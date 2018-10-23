
/*Copyright 2018 kakadila

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <map>

#ifdef WIN32
#define strtok_r strtok_s
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#endif

class HttpRequest {
        struct char_comp  
        {  
                bool operator() (const char *a, const char *b) const  
                {  
                        return strcmp(a,b)<0;
                }  
        };
        typedef std::map<char *,char *,char_comp> StringMap;
        enum ParseStatus{ParsBegin,ParseHead,ParseBody};

        ParseStatus m_pase_status;
        StringMap m_headers;
        int m_content_length;
        char *m_path;
        char *m_method;
        char *m_version;

        char *m_data;
        int m_parse_len;
        int m_offset;
        int m_capacity;
public:
        char *Path(){return m_path;}
        char *Method(){return m_method;};
        int ContentLen(){return m_content_length;}
        char *Data(){return m_data + m_parse_len;}
        char *Header(const char *key)
        {
                StringMap::iterator it  = m_headers.find((char *)key);
                return it==m_headers.end()? NULL : it->second;
        }

        HttpRequest(){
                m_data = (char *)malloc(1024);
                m_capacity = 1024;
                ReSet();
        }
        ~HttpRequest(){free(m_data);}

        void ReSet()
        {
				m_headers.clear();
                m_pase_status = ParsBegin;
                m_content_length = 0;
                m_parse_len = 0;
                m_offset = 0;
        }

        int HandleData(char *buffer, int len) {
                Expand(len);
                memcpy(m_data+m_offset,buffer,len);
                m_offset += len;
                char *start = m_data+m_parse_len;
                m_data[m_offset] = 0;

                while(len > 0)
                {
                        len = m_offset - m_parse_len;
                        if(m_pase_status == ParseBody)
                        {
                                if(len >= m_content_length) return 1;
                                else return 0;
                        }

                        char *end = strchr(start,'\n');
                        int cur_parse_len = end - start + 1;
                        if(end > start && *(end - 1) == '\r')
                        {
                                *end = *(end-1) =  0;
                                m_parse_len += cur_parse_len;
                                if(m_pase_status == ParseHead && ParseLine(start,end) < 0) return -1;
                                if(m_pase_status == ParsBegin && ParsePath(start) <0 ) return -1;
                        }else if(end != NULL)
                                return -1;
                        else return 0;

                        start = end+1;
                }
                return 0;
        }
        void Print()
        {
                printf("=====================\n");
                printf("method : %s\n",m_method);
                printf("path : %s\n",m_path);
                printf("version : %s\n",m_version);
                for(StringMap::iterator it = m_headers.begin();
                        it != m_headers.end();++it)
                {
                        printf("%s:%s\n",it->first,it->second);
                }
                printf("content_len = %d\n",m_content_length);
                if(m_content_length > 0)
                {
                        printf("data : %s\n",m_data + m_parse_len);
                }
        }
private:
        char *trim(char *&start,char *end)
        {       
                while(*start <= 32 && *start > 0) *(start ++ ) = 0;
                while(*end <= 32 && *end > 0) *(end -- ) = 0;
                return start;
        }
        int ParseLine(char *line,char *end)
        {
                if(*line == 0){
                        m_pase_status = ParseBody;
                        return 0;
                }

                char *value = NULL;
                char *key = strtok_r(line,":",&value);
				if(key == NULL || *value == 0) return -1;
                trim(key,value -2);
                trim(value,end);

                if(strcmp(key,"Content-Length")== 0) m_content_length = atoi(value);
                m_headers[key] = value;
                return 0;
        }

        int ParsePath(char *line)
        {
                m_method = strtok_r(line," \t",&m_path);
                if(m_method == NULL || *m_path == 0) return -1;

                m_path = strtok_r(m_path," \t",&m_version);
                if(m_path == NULL || *m_path != '/') return -1;

                m_pase_status = ParseHead;
                return 0;
        }

        void Expand(int len)
        {
                if(len + m_offset > m_capacity)
                {
                        m_capacity = m_capacity + len + 12;
                        char *new_data  = (char *)realloc(m_data,m_capacity);
						//如果mbuffer有relloc过，则指针的内容改变
						if(new_data != m_data)
						{
							m_method = new_data + (m_method- m_data);
							m_version = new_data +(m_version - m_data);
							m_path   = new_data + (m_path - m_data);
							for(StringMap::iterator it = m_headers.begin();
								it!=m_headers.end();++it)
							{
								(char *&)(it->first) = new_data + (it->first - m_data);
								it->second = new_data + (it->second - m_data);
							}
						}
						m_data = new_data;
                }
        }
};

const char *bad_message = "HTTP/1.0 400 Bad Message\r\nServer: XXServer\r\n"
        "Content-Length: 15\r\n\r\n"
        "400 bad message";


const char *not_found_message = "HTTP/1.0 404 Not Found\r\nServer: XXServer\r\n"
        "Content-Length: 13\r\n\r\n"
        "404 not found";

const char *success_head = "HTTP/1.0 200 Ok\r\nServer: XXServer\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n\r\n";

int HandleHttp(HttpRequest &req,int sockfd)
{
        const char *method = req.Method();
        if(strcmp(method,"GET") != 0) 
        {
                return send(sockfd,bad_message,strlen(bad_message),MSG_NOSIGNAL);
        }
        const char *path = req.Path();
        if(strcmp(path,"/") == 0) path = "/index.html";
        char total_path[1024];
        sprintf(total_path,"./html%s",path);

        FILE *fp = fopen(total_path,"r");
        if(fp == NULL) return send(sockfd,not_found_message,strlen(not_found_message),MSG_NOSIGNAL);

        fseek(fp,0,SEEK_END);
        int file_len = ftell(fp);
        rewind(fp);

        char buff[1024] = {0};
  int n = sprintf(buff,success_head,file_len);
        send(sockfd,buff,n,MSG_NOSIGNAL);

        while(file_len > 0)
        {
                memset(buff,0,sizeof(buff));
                n = fread(buff,1,1024,fp);
                if(n <= 0) {printf("close\n");break;}
                send(sockfd,buff,n,MSG_NOSIGNAL);
                file_len -= n;
        }

        fclose(fp);
        return file_len <= 0 ? 0 : -1;
}
void *thread_fun(void * data)
{
        int sockfd = (int)data;
        HttpRequest req;
        while(1)
        {
                char buf[32] = {0};
                int ret = recv(sockfd,buf,32,0);
                if(ret <= 0) 
                {
                        perror("recv");
                        break;
                }
                //printf("%s",buf);
                ret = req.HandleData(buf,ret);
                if(ret < 0) break;
                else if(ret > 0)
                {
                        HandleHttp(req,sockfd);
                        req.ReSet();
                }
        }
        close(sockfd);
        return NULL;
}

void checkerr(int ret,const char *str)
{
        if(ret < 0){
                perror(str);
                exit(1);
        }
}
int main()
{
        int sockfd = socket(AF_INET,SOCK_STREAM,0);
        checkerr(sockfd,"socket");

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(80);
        addr.sin_addr.s_addr = 0;

        checkerr(bind(sockfd,(struct sockaddr *)&addr,sizeof(addr)),"bind");
        checkerr(listen(sockfd,128),"listen");

        while(1)
        {
                int connfd = accept(sockfd,NULL,NULL);
				checkerr(connfd,"accept");
                pthread_t tid;
                pthread_create(&tid,NULL,thread_fun,(void *)connfd);
                pthread_detach(tid);
        }
        return 0;
}
