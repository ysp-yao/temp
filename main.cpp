#include <vector>
#include <iostream>
#include <algorithm>



#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/types.h>
#include <netinet/in.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <unistd.h>




typedef std::vector<struct epoll_event> EventList;

#define ERR_EXIT(m) \
do \
{  \
  perror(m); \
  exit(EXIT_FAILURE); \
} while(0);

int main(void) {

  signal(SIGPIPE, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);

  int idlefd = open("/dev/null", O_RDONLY|O_CLOEXEC);
  int listenfd;

  if ((listenfd=socket(PF_INET, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, IPPROTO_TCP)) < 0)
    ERR_EXIT("socket");

  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(5188);
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

  int on = 1;
  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
    ERR_EXIT("setsockopt");
  
  if (bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0)
    ERR_EXIT("bind");

  if (listen(listenfd, SOMAXCONN) < 0)
    ERR_EXIT("listen");

  std::vector<int> clients;
  int epollfd = epoll_create1(EPOLL_CLOEXEC);

  struct epoll_event event;
  event.data.fd = listenfd;
  event.events = EPOLLIN;
  epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &event);

  EventList events(16);
  struct sockaddr_in peeraddr;
  socklen_t peerlen;
  int connfd;

  int nready;
  while (1) {
    nready = epoll_wait(epollfd, &*events.begin(), static_cast<int>(events.size()), -1);  
    if (nready == -1) {
      if (errno == EINTR)
        continue;

      ERR_EXIT("epoll_wait");
    }
    if (nready == 0)
        continue;

    if ((size_t)nready == events.size())
      events.resize(events.size()*2);

    for (int i=0; i<nready; ++i) {
      if (events[i].data.fd == listenfd) {
        peerlen = sizeof(peeraddr);
        connfd = ::accept4(listenfd, (struct sockaddr*)&peeraddr, &peerlen, SOCK_NONBLOCK|SOCK_CLOEXEC);

        if (connfd == -1) {
          if (errno == ENFILE) {
            close(idlefd);
            idlefd = accept(listenfd, NULL, NULL);
            close(idlefd);
            idlefd = open("/dev/null", O_RDONLY|O_CLOEXEC);
            continue;
          }
          else {
            ERR_EXIT("accept4");
          }
        }
      
        std::cout << "ip=" << inet_ntoa(peeraddr.sin_addr) <<
                    " port=" << ntohs(peeraddr.sin_port) << std::endl;
      
        clients.push_back(connfd);

        event.data.fd = connfd;
        event.events = EPOLLIN;
        epoll_ctl(epollfd, EPOLL_CTL_ADD, connfd, &event);
      }
      else if (events[i].events & EPOLLIN) {
        connfd = events[i].data.fd;
        if (connfd < 0)
          continue;

        char buf[1024] = {0};
        int ret = read(connfd, buf, 1024);
        if (ret == -1)
          ERR_EXIT("read");
        if (ret == 0) {
          std::cout << "client close" << std::endl;
          close(connfd);
          event = events[i];
          epoll_ctl(epollfd, EPOLL_CTL_DEL, connfd, &event);
          clients.erase(std::remove(clients.begin(), clients.end(), connfd), clients.end());
          continue;
        }
  
        std::cout << buf;
        write(connfd, buf, strlen(buf));
      }
    }
  } 

  return 0;
}
