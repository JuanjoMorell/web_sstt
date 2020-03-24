#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define VERSION		24
#define BUFSIZE		8096
#define ERROR		42
#define LOG			44
#define PROHIBIDO	403
#define NOENCONTRADO	404
#define NOACEPTADO		406
#define NOPERMITIDO		405
#define OK				200
#define PERSISTENCIA	1
#define PERS_SEGUNDOS   10

struct {
	char *ext;
	char *filetype;
} extensions [] = {
	{"mp4", "video/mp4" },
	{"jpg", "image/jpg" },
	{"jpeg","image/jpeg"},
	{"png", "image/png" },
	{"ico", "image/ico" },
	{"zip", "image/zip" },
	{"gz",  "image/gz"  },
	{"tar", "image/tar" },
	{"htm", "text/html" },
	{"html","text/html" },
	{0,0} };

void debug(int log_message_type, char *message, char *additional_info, int socket_fd)
{
	int fd ;
	char logbuffer[BUFSIZE*2];
	
	switch (log_message_type) {
		case ERROR: (void)sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",message, additional_info, errno,getpid());
			break;
		case PROHIBIDO:
			// Enviar como respuesta 403 Forbidden
			(void)sprintf(logbuffer,"FORBIDDEN: %s:%s",message, additional_info);
			break;
		case NOENCONTRADO:
			// Enviar como respuesta 404 Not Found
			(void)sprintf(logbuffer,"NOT FOUND: %s:%s",message, additional_info);
			break;
		case LOG: (void)sprintf(logbuffer," INFO: %s:%s:%d",message, additional_info, socket_fd); break;
	}

	if((fd = open("webserver.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
		(void)write(fd,logbuffer,strlen(logbuffer));
		(void)write(fd,"\n",1);
		(void)close(fd);
	}
	if(log_message_type == ERROR || log_message_type == NOENCONTRADO || log_message_type == PROHIBIDO) exit(3);
}

int comprobar_fd(int fd, long int sec, long int usec) {

	struct timeval tv;
	fd_set rfds;
	tv.tv_sec = sec;
	tv.tv_usec = usec;
	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);

	if (select(fd + 1, &rfds, NULL, NULL, &tv) < 0) {
		perror("select()");
		exit(EXIT_FAILURE);
	}
	// Se comprueba si hay cosas por leer aún
	return (FD_ISSET(fd, &rfds));

}

void respuesta(int descriptor, int file, int resp, int nExt) {

	char response[BUFSIZE] = {0};
	struct stat fileStat;

	strcat(response, "HTTP/1.1");
	switch(resp) {
		case 200:
			strcat(response, " 200 OK\r\n");
			break;
		case 404:
			strcat(response, " 404 Not Found\r\n");
			break;
		case 405:
			strcat(response, " 405 Method Not Allowed\r\n");
			break;
		case 406:
			strcat(response, " 406 Not Acceptable\r\n");
			break;
		default:
			strcat(response, " 400 Bad Request\r\n");
			break;
	}
	
	// Tipo de contenido
	if(nExt != -1) {
		strcat(response, "Content-Type: ");
		strcat(response, extensions[nExt].ext);
		strcat(response, "\r\n");
	}
	// Tamaño del fichero
	char contentlenght[128] = {0};
	if(file != -1) {
		fstat(file, &fileStat);
		sprintf(contentlenght, "Content-Lenght: %ld\r\n", fileStat.st_size);
		strcat(response, contentlenght);
	}
	
	strcat(response, "Server: web_sstt\r\n");

	char buf[1000];
	time_t now = time(0);
	struct tm tm = *gmtime(&now);
	strftime(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S %Z\r\n", &tm);
	strcat(response, "Date: ");
	strcat(response, buf);
	strcat(response, "Connection: Keep-Alive\r\n");
	strcat(response, "Keep-Alive: 10\r\n");
	strcat(response, "\r\n");

	printf("%s\n", response);

	write(descriptor, response,  strlen(response));
					
	// Pasar el fichero
	char bufferfile[BUFSIZE] = { 0 };
	int readbytes;

	while( (readbytes = read(file, bufferfile, BUFSIZE-1)) ) {
		write(descriptor, bufferfile, BUFSIZE-1);				
	}
}

void process_web_request(int descriptorFichero)
{
	debug(LOG,"request","Ha llegado una peticion",descriptorFichero);
	
	while(PERSISTENCIA && comprobar_fd(descriptorFichero, PERS_SEGUNDOS, 0)) {
		
			char buffer[BUFSIZE] = {0};
	
			size_t tam_peticion = read(descriptorFichero, buffer, BUFSIZE);
			printf("%s", buffer);

			char mensaje[BUFSIZE] = {0};
			memcpy(mensaje, buffer, strlen(buffer));
			
			if ( tam_peticion < 0 ) {
				close(descriptorFichero);
				debug(ERROR, "system call", "read", 0);
			}
			
			char *primera = strtok(buffer, "\r\n");
			// Obtenemos los valores de la primera linea
			char aux[BUFSIZE] = {0};
			char *metodo, *url, *version;
			char *save_ptr;
			
			strcpy(aux, primera);

			// Obtenemos el metodo
			metodo = strtok_r(primera, " ", &save_ptr);
			// Obtenemos la url
			url = strtok_r(NULL, " ", &save_ptr);
			// Obtenemos la version
			version = strtok_r(NULL, " ", &save_ptr);
			
			//
			//	TRATAR LOS CASOS DE LOS DIFERENTES METODOS QUE SE USAN
			//	(Se soporta solo GET)
			//

			char path[_PC_PATH_MAX] = {0};

			if ( strcmp(metodo, "GET") == 0 ) {

				//Si el contenido de url es / le mandamos al cliente index.html
				if( strcmp(url, "/") == 0 ) {
					strcat(path, "index.html");
					
					int file = open(path, O_RDONLY);
					
					if(file != -1) {
						respuesta(descriptorFichero, file, OK, 9);

						close(file);
					} else {
						// Error tipo  404
						int file = open("error.html", O_RDONLY);
						respuesta(descriptorFichero, file, NOENCONTRADO, 9);
						close(file);

					}
				} else {
					// El cliente pide un archivo distinto al index.html
					url++;
					strcat(path, url);
					
					char *extension;
					int nExtension = -1;

					extension = url + (strlen(url) - 3);

					for(int i = 0; i < 10; i++) {
						if(strcmp(extensions[i].ext, extension) == 0) {
							nExtension = i;
							break;
						} 
					}
					if (nExtension == -1) {
						// Error tipo 406
						respuesta(descriptorFichero, -1, NOACEPTADO, -1);
					}

					int file = open(path, O_RDONLY);

					if (file != -1) {
						respuesta(descriptorFichero, file, OK, nExtension);
						close(file);		
					} else {
						// Error tipo 404
						respuesta(descriptorFichero, -1, NOENCONTRADO, -1);
					}
				} 
			} else if (strcmp(metodo, "POST") == 0) {
				char delim[] = "\n\n";
				char *ptr = strtok(mensaje, delim);

				while( ptr != NULL ) {
					ptr = strtok(NULL, delim);
					if(strstr(ptr, "email")) break;
				}
				
				size_t tam = strlen(ptr);
				if ( tam == 6 ) {
					printf("No se ha escrito un correo\n");
					int file = open("index_fallo.html", O_RDONLY);
					
					if (file != -1) {
						respuesta(descriptorFichero, file, OK, 9);
						close(file);
					} else {
						// Error tipo 404
						respuesta(descriptorFichero, -1, NOENCONTRADO, -1);
					}
				}

				char *email = strtok(ptr, "=");
				email = strtok(NULL, "=");

				int file = open("correo_error.html", O_RDONLY);
				
				// @ == %40
				if (strstr(email, "juanjose.morellf%40um.es") ) {
					file = open("correo_ok.html", O_RDONLY);
				}
				if (file != -1) {
					respuesta(descriptorFichero, file, OK, 9);
				
					close(file);
				} else {
					// Error tipo 404
					respuesta(descriptorFichero, -1, NOENCONTRADO, -1);
				}

			} else {
				// Error tipo 400
				respuesta(descriptorFichero, -1, 400, -1);
			}
	}
	printf("Fin conexion\n");
	debug(LOG, "conexión", "cerrada", descriptorFichero);
	exit(1);
}

int main(int argc, char **argv)
{
	int i, port, pid, listenfd, socketfd;
	socklen_t length;
	static struct sockaddr_in cli_addr;		// static = Inicializado con ceros
	static struct sockaddr_in serv_addr;	// static = Inicializado con ceros
	
	//  Argumentos que se esperan:
	//
	//	argv[1]
	//	En el primer argumento del programa se espera el puerto en el que el servidor escuchara
	//
	//  argv[2]
	//  En el segundo argumento del programa se espera el directorio en el que se encuentran los ficheros del servidor
	//
	//  Verficiar que los argumentos que se pasan al iniciar el programa son los esperados
	//

	//
	//  Verficiar que el directorio escogido es apto. Que no es un directorio del sistema y que se tienen
	//  permisos para ser usado
	//

	if(chdir(argv[2]) == -1){ 
		(void)printf("ERROR: No se puede cambiar de directorio %s\n",argv[2]);
		exit(4);
	}
	// Hacemos que el proceso sea un demonio sin hijos zombies
	if(fork() != 0)
		return 0; // El proceso padre devuelve un OK al shell

	(void)signal(SIGCHLD, SIG_IGN); // Ignoramos a los hijos
	(void)signal(SIGHUP, SIG_IGN); // Ignoramos cuelgues
	
	debug(LOG,"web server starting...", argv[1] ,getpid());
	
	/* setup the network socket */
	if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0)
		debug(ERROR, "system call","socket",0);
	
	port = atoi(argv[1]);
	
	if(port < 0 || port >60000)
		debug(ERROR,"Puerto invalido, prueba un puerto de 1 a 60000",argv[1],0);
	
	/*Se crea una estructura para la información IP y puerto donde escucha el servidor*/
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); /*Escucha en cualquier IP disponible*/
	serv_addr.sin_port = htons(port); /*... en el puerto port especificado como parámetro*/
	
	if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0)
		debug(ERROR,"system call","bind",0);
	
	if( listen(listenfd,64) <0)
		debug(ERROR,"system call","listen",0);
	
	while(1){
		length = sizeof(cli_addr);
		if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			debug(ERROR,"system call","accept",0);
		if((pid = fork()) < 0) {
			debug(ERROR,"system call","fork",0);
		}
		else {
			if(pid == 0) { 	// Proceso hijo
				(void)close(listenfd);
				process_web_request(socketfd); // El hijo termina tras llamar a esta función
			} else { 	// Proceso padre
				(void)close(socketfd);
			}
		}
	}
}
