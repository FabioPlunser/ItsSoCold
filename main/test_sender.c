#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_HOST "pbl.permasense.uibk.ac.at"
#define SERVER_PORT 22504

int main() {
    // Generate sample temperature (simulating sensor reading)
    float temperature = 23.5678;  // Example temperature

    // Format data string
    char post_data[128];
    time_t now;
    struct tm *timeinfo;
    
    time(&now);
    timeinfo = localtime(&now);
    
    // Format: YYYY-MM-DD HH:MM:SS+0000,GROUP_ID,TEMPERATURE,COMMENT
    snprintf(post_data, sizeof(post_data), 
             "%04d-%02d-%02d %02d:%02d:%02d+0000,1,%.4f,yes it works\n",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec,
             temperature);

    // DNS Resolution
    struct hostent *host = gethostbyname(SERVER_HOST);
    if (!host) {
        printf("DNS resolution failed\n");
        return 1;
    }

    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Socket creation error\n");
        return 1;
    }

    // Connect
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(SERVER_PORT),
        .sin_addr = *((struct in_addr *)host->h_addr)
    };

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("Connection failed\n");
        close(sock);
        return 1;
    }

    // Send data
    if (send(sock, post_data, strlen(post_data), 0) < 0) {
        printf("Send failed\n");
    } else {
        printf("Data sent successfully: %s", post_data);
    }

    close(sock);
    return 0;
}