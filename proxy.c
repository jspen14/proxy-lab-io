#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

/* RECOMMENDED DEFINITIONS */
#define MAXEVENTS 40
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define STRING_SIZE 128

/* STRUCT DEFINITIONS */
typedef struct cache_item cache_item;

struct cache_item {
  char *request;
  char *response;
  int response_size;
  cache_item *nextPtr;
};

struct request_info {
  int client_sfd;
  int server_sfd;
  char *client_request;
  char *state;
  char *buffer;
  int bytes_read_from_client;
  int bytes_to_write_to_server;
  int bytes_written_to_server;
  int bytes_read_from_server;
  int bytes_written_to_client;
};

struct server_req_info{
  char *request_type;
  char *host;
  char *port;
  char *path;
  char *headers;
};

/* GLOBAL VARIABLES */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *p_connection_hdr = "Proxy-Connection: close\r\n";

struct cache_item *cache_head;
int current_cache_size;

void print_request_info(struct request_info *info){
  fprintf(stderr, "[\n\tclient_sfd: %d\n", info->client_sfd);
  fprintf(stderr, "\tserver_sfd: %d\n", info->server_sfd);
  fprintf(stderr, "\tserver_request: \n%s\n", info->client_request);
  fprintf(stderr, "\tstate: %s\n", info->state);
  fprintf(stderr, "\tbuffer: \n%s\n", info->buffer);
  fprintf(stderr, "\tbytes_read_from_client: %d\n", info->bytes_read_from_client);
  fprintf(stderr, "\tbytes_to_write_to_server: %d\n", info->bytes_to_write_to_server);
  fprintf(stderr, "\tbytes_written_to_server: %d\n", info->bytes_written_to_server);
  fprintf(stderr, "\tbytes_read_from_server: %d\n", info->bytes_read_from_server);
  fprintf(stderr, "\tbytes_written_to_client: %d\n", info->bytes_written_to_client);
  fprintf(stderr, "]\n\n");
}

int getListenSocket(int argc, char **argv){
  struct addrinfo hints;
  struct addrinfo *result, *rp;
  char *portArg = malloc(64*sizeof(char));
  int portNum, getaddrinfo_return, listenfd;

  if(argc > 1){
    portNum = atoi(argv[1]);
  }
  else{
    portNum = 80;
  }

  sprintf(portArg,"%d",portNum);

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_STREAM; /* For UDP: SOCK_DGRAM Datagram socket */
  hints.ai_flags = AI_PASSIVE;    /* For wildcard IP address */
  hints.ai_protocol = 0;          /* Any protocol */
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;

  getaddrinfo_return = getaddrinfo(NULL, portArg, &hints, &result);

  if (getaddrinfo_return != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getaddrinfo_return));
    exit(EXIT_FAILURE);
  }

  for (rp = result; rp != NULL; rp = rp->ai_next) {

    listenfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

    if (listenfd == -1){
      printf("\tFailed binding\n");
      fflush(stdout);
      continue;
    }

    if (bind(listenfd, rp->ai_addr, rp->ai_addrlen) == 0){
      break;                  /* Success */
    }
  }

  if (rp == NULL) {               /* No address succeeded */
    fprintf(stderr, "Could not bind\n");
    exit(EXIT_FAILURE);
  }

  if (listen(listenfd, 100) < 0) {
      close(listenfd);
      return -1;
  }
  else{
    return listenfd;
  }

}

char *get_port(char *host){
  char *portArg = malloc(MAX_OBJECT_SIZE*sizeof(char));
  char buf[10];
  int portNo = 0;
  int host_index = 0, buf_index = 0, cut_index = 0;

  if(strchr(host,':') == NULL){
    portNo = 80;
  }
  else{
    //consume up until colon
    while(host[host_index] != ':'){
      host_index += 1;
    }

    cut_index = host_index;

    //consume colon
    host_index += 1;

    while(host_index<strlen(host)){
      buf[buf_index] = host[host_index];
      buf_index += 1;
      host_index += 1;
    }

    //Remove the port stuff from the hosts
    host[cut_index] = 0x00;

    portNo = atoi(buf);
  }

  sprintf(portArg,"%d",portNo);

  return portArg;
}

struct server_req_info parse_client_request(char *client_request, int request_size){
  struct server_req_info *parsed_client = malloc(sizeof(struct server_req_info));
  int req_index = 0;
  int buf_index = 0;

  //Get the request type
  char *type = malloc(MAX_OBJECT_SIZE*sizeof(char));
  while(client_request[req_index] != ' '){
    type[buf_index] = client_request[req_index];
    buf_index += 1;
    req_index += 1;
  }
  type[buf_index] = 0x00;
  parsed_client->request_type = type;

  //Consume input until we get a forward slash
  while(client_request[req_index] != '/'){
    req_index += 1;
  }

  //Consume the "//""
  req_index += 2;

  //Get the host
  char *host = malloc(MAX_OBJECT_SIZE*sizeof(char));
  buf_index = 0;

  while(client_request[req_index] != '/'){
    host[buf_index] = client_request[req_index];
    buf_index += 1;
    req_index += 1;
  }
  host[buf_index] = 0x00;

  parsed_client->host = host;
  parsed_client->port = get_port(parsed_client->host);

  //Get the pathfreeaddrinfo
  char *path = malloc(MAX_OBJECT_SIZE*sizeof(char));
  buf_index = 0;

  while(client_request[req_index] != ' '){
    path[buf_index] = client_request[req_index];
    buf_index += 1;
    req_index += 1;

  }
  path[buf_index] = 0x00;
  parsed_client->path = path;

  //If I'm supposed to read anything else, then I can read it into another buffer and set the headers param of the struct
  char *strBuilder = malloc(MAX_OBJECT_SIZE*sizeof(char));
  strcat(strBuilder,"http://");
  strcat(strBuilder,parsed_client->host);
  strcat(strBuilder,":");
  strcat(strBuilder,parsed_client->port);
  strcat(strBuilder,parsed_client->path);

  return *parsed_client;
}

char *construct_server_request(struct server_req_info parsed_client, struct request_info *info){
    char *server_request = malloc(MAX_OBJECT_SIZE*sizeof(char));

    //Append the type
    strcat(server_request,parsed_client.request_type);
    strcat(server_request," ");
    strcat(server_request,parsed_client.path);
    strcat(server_request," HTTP/1.0\r\n");

    //Host
    strcat(server_request,"Host: ");
    strcat(server_request,parsed_client.host);
    strcat(server_request,"\r\n");

    //User-Agent
    strcat(server_request,user_agent_hdr);

    //Connection
    strcat(server_request,connection_hdr);

    //Proxy-Connection
    strcat(server_request,p_connection_hdr);

    //Close request
    strcat(server_request,"\r\n");

    return server_request;
}

char *check_cache(char *requestToCheck, struct request_info *info){
  char *retVal = malloc(MAX_OBJECT_SIZE*sizeof(char));

  if(cache_head == NULL){

    return NULL;
  }

  cache_item *temp = cache_head;
  int done = 0;

  while (!done){

    if(!strcmp(temp->request,requestToCheck)){

      retVal = temp->response;
      info->bytes_read_from_server = temp->response_size;
      done = 1;
    }
    else if(temp->nextPtr == NULL){

      retVal = NULL;
      done = 1;
    }
    else{

      temp = temp->nextPtr;
    }
  }

  return retVal;
}

void cache_insert( struct request_info *info){
  char *request = info->client_request;
  char *response = info->buffer;
  int response_size = info->bytes_read_from_server;

  if( response_size + current_cache_size >= MAX_CACHE_SIZE){

     return;
  }

  if (cache_head == NULL){
    cache_head = malloc(sizeof(struct cache_item));
    cache_head->request = malloc(MAX_OBJECT_SIZE * sizeof(char));
    cache_head->request = request;
    cache_head->response = malloc(MAX_OBJECT_SIZE * sizeof(char));
    cache_head->response = response;
    cache_head->response_size = response_size;
    cache_head->nextPtr = NULL;
  }
  else{

    int done = 0;

    cache_item *temp = cache_head;

    while(!done){

        if(temp->nextPtr == NULL){

          cache_item *next = malloc(sizeof(cache_item));
          next->request = malloc(MAX_OBJECT_SIZE * sizeof(char));
          next->request = request;
          next->response = malloc(MAX_OBJECT_SIZE * sizeof(char));
          next->response = response;
          next->response_size = response_size;
          next->nextPtr = NULL;
          temp->nextPtr = next;

          done = 1;
        }
        else{

          temp = temp->nextPtr;
        }
    }
  }

  return;
}

void create_server_connection(struct server_req_info parsed_client, struct request_info *info){
  struct addrinfo servaddr;
  struct addrinfo *result, *rp;
	int sfd, s;

  //Create a TCP socket connection
  memset(&servaddr, 0, sizeof(struct addrinfo));
	servaddr.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
	servaddr.ai_socktype =  SOCK_STREAM; /* TCP socket */
	servaddr.ai_flags = 0;
	servaddr.ai_protocol = 0;

  //If the host contains a port I need to connect to that instead of port 80
  s = getaddrinfo(parsed_client.host, parsed_client.port, &servaddr, &result); //the second argument might not work
  if (s != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
    exit(EXIT_FAILURE);
  }

  for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

    if (sfd == -1){
      continue;
    }

		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1){
      break;            /* Success */
    }
    else {
      close(sfd);
    }

	}

	if (rp == NULL) {               /* No address succeeded */
		fprintf(stderr, "Could not connect\n");
		exit(EXIT_FAILURE);
	}

  // Configure socket to use non-blocking I/O
  if (fcntl(sfd, F_SETFL, fcntl(sfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
    fprintf(stderr, "Error setting socket to non-blocking.\n");
    exit(1);
  }


  info->server_sfd = sfd;

	freeaddrinfo(result);           /* No longer needed */
}

void handle_client_READ_REQUEST(int *efd, struct epoll_event event, struct request_info *info, FILE *fptr){
  // Initialize variables
  int nread;
  char *temp_buffer = malloc(MAX_OBJECT_SIZE * sizeof(char));

  // Read loop
  for(;;){
    nread = recv(info->client_sfd,temp_buffer,MAX_OBJECT_SIZE,0);
    temp_buffer[nread] = 0x00;
    info->bytes_read_from_client += nread;

    if (nread == 0){
      break;
    }
    else if (nread < 0){

      if(errno == EWOULDBLOCK || errno == EAGAIN){
        fprintf(stderr, "READ_REQUEST errno == EWOULDBLOCK || EAGAIN\n");
      }
      else {
        epoll_ctl(*efd, EPOLL_CTL_DEL, info->client_sfd, &event);
        close(info->client_sfd);
      }
      break;
    }
    else{
      strcat(info->buffer, temp_buffer);
    }

    if(strstr(info->buffer,"\r\n\r\n") != NULL){

      char *client_request = malloc(info->bytes_read_from_client);
      memcpy(client_request, info->buffer, info->bytes_read_from_client);

      // Parse client request
      struct server_req_info parsed_client = parse_client_request(info->buffer, info->bytes_read_from_client);

      // Log the request
      fprintf(fptr, "IP: %s\n", client_request);

      // Construct the server request
      char *server_request = construct_server_request(parsed_client, info);


      // Check cache
      char *cache_return = "";
      if ((cache_return = check_cache(client_request, info)) != NULL){

        event.data.ptr = info;
        event.events = EPOLLOUT | EPOLLET;

        if (epoll_ctl(*efd, EPOLL_CTL_MOD, info->client_sfd, &event) < 0) {
          fprintf(stderr, "Error adding event to efd (2): %s\n", strerror(errno));

          // Clean up server AND CLIENT
          epoll_ctl(*efd, EPOLL_CTL_DEL, info->client_sfd, &event);
          close(info->client_sfd);
          info->client_sfd = -1;

          return;
        }

        //Register epoll event for reading
        info->state = "SEND_RESPONSE";
        memset(info->buffer, 0, MAX_OBJECT_SIZE*sizeof(char));
        info->buffer = cache_return;
      }
      else {

        info->client_request = client_request;
        info->state = "SEND_REQUEST";
        info->bytes_to_write_to_server = strlen(server_request);
        memset(info->buffer, 0, MAX_OBJECT_SIZE*sizeof(char));
        info->buffer = server_request;

        // Set up a new socket
        create_server_connection(parsed_client, info);

        // Register the socket with epoll instance for writing
        event.data.ptr = info;
        event.events = EPOLLOUT | EPOLLET;
        if (epoll_ctl(*efd, EPOLL_CTL_ADD, info->server_sfd , &event) < 0) {
          fprintf(stderr, "Error adding event to efd.\n");
          exit(1);
        }
        else {
          break;
        }

      }
    }
    else {
      /* Need to continue reading */
    }
  }
}

void handle_client_SEND_REQUEST(int *efd, struct epoll_event event, struct request_info *info){
  // Initialize Variables
  int bytes = 0;

  // Write loop
  do {
    // bytes = write(sfd,server_request+sent,total-sent);
    bytes = write(info->server_sfd, info->buffer + info->bytes_written_to_server, info->bytes_to_write_to_server-info->bytes_written_to_server);

    if (bytes < 0){

      if(errno == EWOULDBLOCK || errno == EAGAIN){
        fprintf(stderr, "(3)\n");
      }
      else {
        epoll_ctl(*efd, EPOLL_CTL_DEL, info->client_sfd, &event);
        epoll_ctl(*efd, EPOLL_CTL_DEL, info->server_sfd, &event);
        close(info->client_sfd);
        close(info->server_sfd);
        info->client_sfd = -1;
        info->server_sfd = -1;
      }

      return; // Changed this to return from break
    }
    else{
      info->bytes_written_to_server += bytes;
    }

  } while (info->bytes_written_to_server < info->bytes_to_write_to_server);

  // Register the socket with the epoll instance for reading
  event.data.ptr = info;
  event.events = EPOLLIN | EPOLLET;
  if (epoll_ctl(*efd, EPOLL_CTL_MOD, info->server_sfd, &event) < 0) {
    fprintf(stderr, "Error adding event to efd (1): %s\n", strerror(errno));
    exit(1);
  }


  // Change state to READ_RESPONSE
  info->state = "READ_RESPONSE";

  // Clear buffer to READ_RESPONSE
  memset(info->buffer,0,sizeof(MAX_OBJECT_SIZE));

}

void handle_client_READ_RESPONSE(int *efd, struct epoll_event event, struct request_info *info){

  // Initialize variables
  int total = MAX_OBJECT_SIZE-1;
  int bytes = 0;

  // Read loop
  for(;;) {
      // bytes = read(sfd,response+received,total-received);
      bytes = read(info->server_sfd, info->buffer + info->bytes_read_from_server, total - info->bytes_read_from_server);

      if (bytes < 0){
        if(errno == EWOULDBLOCK || errno == EAGAIN){

        }
        else {
          // Clean up efd
          epoll_ctl(*efd, EPOLL_CTL_DEL, info->client_sfd, &event);
          epoll_ctl(*efd, EPOLL_CTL_DEL, info->server_sfd, &event);
          close(info->client_sfd);
          close(info->server_sfd);
          info->client_sfd = -1;
          info->server_sfd = -1;
        }

        return; // Changed this to return from break

      }
      else if (bytes == 0 || info->bytes_read_from_server >= total){

        // Register the client socket with the epoll instance for writing
        event.data.ptr = info;
        event.events = EPOLLOUT | EPOLLET;

        if (epoll_ctl(*efd, EPOLL_CTL_MOD, info->client_sfd, &event) < 0) {
          fprintf(stderr, "Error adding event to efd (2): %s\n", strerror(errno));

          // Clean up server AND CLIENT
          epoll_ctl(*efd, EPOLL_CTL_DEL, info->client_sfd, &event);
          close(info->client_sfd);
          info->client_sfd = -1;

          epoll_ctl(*efd, EPOLL_CTL_DEL, info->server_sfd, &event);
          close(info->server_sfd);
          info->server_sfd = -1;

          return;
        }

        // Clean up efd of server_sfd
        epoll_ctl(*efd, EPOLL_CTL_DEL, info->server_sfd, &event);
        close(info->server_sfd);
        info->server_sfd = -1;


        // Change state to send_response
        info->state = "SEND_RESPONSE";

        // Basic error check
        if (info->bytes_read_from_server == total){
          printf("ERROR storing complete response from socket");
        }

        // Insert into cache
        cache_insert(info);

        return;
      }
      else{
        info->bytes_read_from_server += bytes;
      }
  }

}

void handle_client_SEND_RESPONSE(int *efd, struct epoll_event event, struct request_info *info){
	// NOT MAKING IT HERE
  // Initialize variables
  int bytes = 0;

  fprintf(stderr, "IN SEND RESPONSE!\n");
  print_request_info(info);

  // Write loop
  for (;;) {
        bytes = write(info->client_sfd, info->buffer + info->bytes_written_to_client, info->bytes_read_from_server - info->bytes_written_to_client);

        fprintf(stderr, "A\n");
        if (bytes < 0){
          fprintf(stderr, "B\n");

          if(errno == EWOULDBLOCK || errno == EAGAIN){
            fprintf(stderr, "C\n");
          }
          else {
            fprintf(stderr, "D\n");
            epoll_ctl(*efd, EPOLL_CTL_DEL, info->client_sfd, &event);
            close(info->client_sfd);
            info->client_sfd = -1;
            fprintf(stderr, "E\n");
          }

          fprintf(stderr, "F\n");
          return; // Changed to return from break
        }
        else if (bytes == 0){
          fprintf(stderr, "G\n");

          epoll_ctl(*efd, EPOLL_CTL_DEL, info->client_sfd, &event);
          close(info->client_sfd);
          info->client_sfd = -1;

          return; // Changed to return from break
        }
        else{
          fprintf(stderr, "H\n");
          info->bytes_written_to_client += bytes;
        }
        fprintf(stderr, "I\n");

  }
  fprintf(stderr, "J\n");

  fprintf(stderr, "K\n");

}


int main(int argc, char **argv)
{
  // Initialize variables
  int efd, n, listenfd, connfd;
  struct epoll_event event;
  struct epoll_event *events;
  FILE *fptr;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  // Create an epoll instance with epoll_create1
  if ((efd = epoll_create1(0)) < 0) {
    fprintf(stderr, "Error creating epoll fd.\n");
    exit(1);
  }

  // Set up listen socket
  listenfd = getListenSocket(argc, argv);

  // Configure it to use non-blocking I/O
  if (fcntl(listenfd, F_SETFL, fcntl(listenfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
    fprintf(stderr, "Error setting socket to non-blocking.\n");
    exit(1);
  }

  // Register listenfd with efd instance for reading
  struct request_info *info = malloc(sizeof(struct request_info));
  info->server_sfd = listenfd;
  event.data.ptr = info;
  event.events = EPOLLIN | EPOLLET;
  if (epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &event) < 0) {
    fprintf(stderr, "Error adding event to efd.\n");
    exit(1);
  }

  // Open log file
  fptr = fopen("./proxy.log","w");

  if(fptr == NULL){
    fprintf(stderr, "Error initializing log file.\n");
    exit(1);
  }

  // Initialize cache
  cache_head = NULL;
  current_cache_size = 0;

  // Start the epoll wait loop, timeout of 1 second
	events = calloc(MAXEVENTS, sizeof(event));

  while (1) {
		n = epoll_wait(efd, events, MAXEVENTS, 1000); // timeout argument specifies number of milliseconds

    // If the result was a timeout
    if (n == 0){
      // Check if global flag has been set by handler
      if(errno == EWOULDBLOCK || errno == EAGAIN){
        // says we should break out of the loop here

      }
      else{
        continue;
      }
    }
    else if (n < 0){
      fprintf(stderr, "ENTERED ERROR IF\n");
      if (errno == EBADF) {
        fprintf(stderr, "EBADF Error.\n");
      }
      else if (errno == EFAULT) {
        fprintf(stderr, "EFAULT Error.\n");
      }
      else if (errno == EINTR) {
        fprintf(stderr, "EINTR Error.\n");
      }
      else {
        fprintf(stderr, "EINVAL Error.\n");
      }
      exit(1);
    }
    else {
      for (int i = 0; i < n; i++) {

        if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (events[i].events & EPOLLRDHUP)) {
          /* An error has occured on this fd */
          fprintf (stderr, "epoll error on fd %d\n", events[i].data.fd);
          close(events[i].data.fd);
          continue;
        }

        struct request_info *info = events[i].data.ptr;
        event = events[i];

        if (listenfd == info->server_sfd) {

          while ((connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen)) > 0) {

            // Configure socket to use non-blocking I/O
            if (fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
              fprintf(stderr, "Error setting connfd to non-blocking\n");
              exit(1);
            }

            // Handle new clients
            struct request_info *info_cli = malloc(sizeof(struct request_info));

            // Initialize variables
            info_cli->client_sfd = connfd;
            info_cli->server_sfd = -1;

            info_cli->state = malloc(MAX_OBJECT_SIZE * sizeof(char));
            memset(info_cli->state, 0, MAX_OBJECT_SIZE * sizeof(char));
            info_cli->state = "READ_REQUEST";

            info_cli->buffer = malloc(MAX_OBJECT_SIZE * sizeof(char));
            memset(info_cli->buffer, 0, MAX_OBJECT_SIZE * sizeof(char));

            info_cli->client_request = malloc(MAX_OBJECT_SIZE * sizeof(char));
            memset(info_cli->client_request, 0, MAX_OBJECT_SIZE * sizeof(char));

            info_cli->bytes_read_from_client = 0;
            info_cli->bytes_to_write_to_server = 0;
            info_cli->bytes_written_to_server = 0;
            info_cli->bytes_read_from_server = 0;
            info_cli->bytes_written_to_client = 0;

            // Add event to epoll file descriptor
            event.data.ptr = info_cli;
            event.events = EPOLLIN | EPOLLET;
            if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &event) < 0) {
              fprintf(stderr, "Error adding event to efd\n");
              exit(1);
            }


          }

          if (errno == EWOULDBLOCK || errno == EAGAIN) {
            continue;
          } else {
            perror("Error accepting in handle_listenfd_event.\n");
          }
        }
        else {
          if (!strcmp(info->state, "READ_REQUEST")) {
            handle_client_READ_REQUEST(&efd, event, info, fptr);
          }
          else if (!strcmp(info->state, "SEND_REQUEST")) {
            handle_client_SEND_REQUEST(&efd, event, info);
          }
          else if (!strcmp(info->state, "READ_RESPONSE")) {
            handle_client_READ_RESPONSE(&efd, event, info);

          }
          else if (!strcmp(info->state, "SEND_RESPONSE")) {
            fprintf(stderr, "STATE IS SEND_RESPONSE\n");
            handle_client_SEND_RESPONSE(&efd, event, info);
          }
          else {
            fprintf(stderr, "Unrecognized state.\n");
            exit(1);
          }
        }
      }
    }
  }


	free(events);


  printf("\n%s\n",user_agent_hdr);
  return 0;
}
