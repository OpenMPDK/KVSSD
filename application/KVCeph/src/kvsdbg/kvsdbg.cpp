
#include "kvsdbg.h"

        KvsDebugTerminal(): sock(0), connected(false) {
	}

        bool KvsDebugTerminal::connect(const std::string &ip, int port) {

                struct sockaddr_in serv_addr;
                if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
                {
                        printf("\n Socket creation error \n");
                        return false;
                }

                memset(&serv_addr, '0', sizeof(serv_addr));

                serv_addr.sin_family = AF_INET;
                serv_addr.sin_port = htons(port);

                // Convert IPv4 and IPv6 addresses from text to binary form
                if(::inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr)<=0)
                {
                        printf("\nInvalid address/ Address not supported \n");
                        return false;
                }

                if (::connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
                {
                        printf("\nConnection Failed \n");
                        return false;
                }
                connected = true;
                return true;

        }

        bool KvsDebugTerminal::sendmsg_ex(const std::string fmt, ...) {
                if (!connected) return false;
                int size = ((int)fmt.size()) * 2 + 50;   // Use a rubric appropriate for your code
                std::string str;
                va_list ap;
                while (1) {     // Maximum two passes on a POSIX system...
                        str.resize(size);
                        va_start(ap, fmt);
                        int n = vsnprintf((char *)str.data(), size, fmt.c_str(), ap);
                        va_end(ap);
                        if (n > -1 && n < size) {  // Everything worked
                                str.resize(n);
                                return sendmsg(str);
                        }
                        if (n > -1)  // Needed size returned
                                size = n + 1;   // For null char
                        else
                                size *= 2;      // Guess at a larger size (OS specific)
                }
                return sendmsg(str);
        }

        bool KvsDebugTerminal::sendmsg(const std::string &msg) {
                if (!connected) return false;
                send(sock , msg.c_str() , msg.length() , 0 );
                return true;
        }

