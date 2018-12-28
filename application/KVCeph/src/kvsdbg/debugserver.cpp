#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define BUFFER_SIZE 2048
#define on_error(...) { fprintf(stderr, __VA_ARGS__); fflush(stderr); exit(1); }
//Example code: A simple server side code, which echos back the received message.
//Handle multiple socket connections with select and fd_set on Linux 
#include <stdio.h> 
#include <string.h>   //strlen 
#include <stdlib.h> 
#include <errno.h> 
#include <unistd.h>   //close 
#include <arpa/inet.h>    //close 
#include <sys/types.h> 
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <sys/time.h> //FD_SET, FD_ISSET, FD_ZERO macros 
#include <unistd.h>
 
#define TRUE   1 
#define FALSE  0 

#if 1

long readLine(int fd, char *buffer, size_t n)
{
    long numRead;                    /* # of bytes fetched by last read() */
    long totRead;                     /* Total bytes read so far */
    char *buf;
    char ch;

    if (n <= 0 || buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    buf = buffer;                       /* No pointer arithmetic on "void *" */

    totRead = 0;
    for (;;) {
        numRead = read(fd, &ch, 1);
        if (numRead == -1) {
            if (errno == EINTR)         /* Interrupted --> restart read() */
                continue;
            else
                return -1;              /* Some other error */

        } else if (numRead == 0) {      /* EOF */
            if (totRead == 0)           /* No bytes read; return 0 */
                return 0;
            else                        /* Some bytes read; add '\0' */
                break;

        } else {                        /* 'numRead' must be 1 if we get here */
            if (totRead < n - 1) {      /* Discard > (n - 1) bytes */
                totRead++;
                *buf++ = ch;
            }

            if (ch == '\n')
                break;
        }
    }

    *buf = '\0';
    return totRead;
}
#include <sstream>

void *client_thread(void *socket) {
        char clientname[256]; 
  	char buf[BUFFER_SIZE+1];
	int sd = (int)(long)socket;	
	FILE *fp;

	// read header
	do {
		std::stringstream ss;
		ss << "log_";

		long read = readLine(sd, clientname, 256);
		if (read > 0) {
			clientname[read-1] = '\0';
		}
		ss << clientname;
		ss << ".txt";
		std::string filename = ss.str();
		fp = fopen(filename.c_str(), "a");
		fprintf(stderr, "opening a file : %s\n", filename.c_str());
                fprintf(fp, "%s connected\n", clientname);

	} while (0);
	
	bool isrados = false;
	if (strcmp(clientname, "osd") == 0) {
		isrados = true;
	}

	while (1) {
	      long read = readLine(sd, buf, BUFFER_SIZE);

	      if (!read) break; // done reading
	      if (read < 0) break;

	      //buf[read] = '\0';
              fprintf(fp, "%s", buf);
	      fflush(fp);
	      if (isrados) {
              	fprintf(stderr, "[%s] %s",clientname, buf);
	      }
	}
  	fprintf(fp, "%s disconnected.\n", clientname);
	fclose(fp);
  	printf("%s disconnected.\n", clientname);
}
 
int main (int argc, char *argv[]) {
  if (argc < 2) on_error("Usage: %s [port]\n", argv[0]);

  int port = atoi(argv[1]);

  int server_fd,  err;
  struct sockaddr_in server, client;

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) on_error("Could not create socket\n");

  server.sin_family = AF_INET;
  server.sin_port = htons(port);
  server.sin_addr.s_addr = htonl(INADDR_ANY);

  int opt_val = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof opt_val);

  err = bind(server_fd, (struct sockaddr *) &server, sizeof(server));
  if (err < 0) on_error("Could not bind socket\n");

  err = listen(server_fd, 128);
  if (err < 0) on_error("Could not listen on socket\n");

  printf("Server is listening on %d\n", port);

  pthread_t tids[1024];
  int threads = 0;
  while (1) {
    socklen_t client_len = sizeof(client);
    int client_fd = accept(server_fd, (struct sockaddr *) &client, &client_len);

    if (client_fd < 0) on_error("Could not establish new connection\n");

    printf("client is connected: sd = %d.\n", client_fd);

    pthread_create( &tids[threads], NULL, client_thread, (void *)(long)client_fd);
    threads++;
  }

  for (int i =0; i < threads; i++) {
	pthread_join(tids[i], 0);
  }

  return 0;
}
#endif
