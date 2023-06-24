#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <string.h>   //strlen
extern void *memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen);
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>   //close
#include <arpa/inet.h>    //close
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h> //FD_SET, FD_ISSET, FD_ZERO macros
#include <fcntl.h> //fcntl()
#include <time.h>
#include <signal.h>
#include <sys/wait.h> // waitpid()
#include <sys/stat.h> // mkdir()
#include <dirent.h> // for struct dirent
#include <inttypes.h>

#define MAX_CLIENTS 255
#define STATIC_404 "<!DOCTYPE html>"\
			"<html><head><style>h1 { color: red; }</style>"\
			"<title>Page Not Found</title></head>"\
			"<body><h1>404 Not Found</h1>"\
			"<b>The requested URL was not found on the server!</b><br>"\
			"</body></html>"

int afd = -1, sfd = -1;
int client[MAX_CLIENTS];
char *h_buffer = NULL;
unsigned char *a_buffer = NULL;
int stricmp(const char *a, const char *b)
{
	int ca,cb;
	do
	{
		ca = (unsigned char) *a++;
		cb = (unsigned char) *b++;
		if(ca >= 'A' && ca <= 'Z')
			ca += 0x20;
		if(cb >= 'A' && cb <= 'Z')
			cb += 0x20;
	} while (ca == cb && ca != '\0');
	return ca - cb;
}
#define IMAX_BITS(m) ((m)/((m)%255+1) / 255%255*8 + 7-86/((m)%255+12))
_Static_assert((RAND_MAX & (RAND_MAX + 1u)) == 0, "RAND_MAX not a Marsenne number");
uint64_t rand_uint64_t()
{
	uint64_t r = 0;
	for(int i = 0; i < 64; i += IMAX_BITS(RAND_MAX))
	{
		r <<= IMAX_BITS(RAND_MAX);
		r ^= (unsigned) rand();
	}
	return r;
}

struct http_req
{
	char* request_type;
	char* url;
	char* http_type;
	int num_header;
	char** header;
	char** value;
	int num_cookie;
	char** cookiename;
	char** cookievalue;
	size_t content_size;
	char* content;
};

void destroy_http_req(struct http_req obj)
{
	//if(obj == NULL)
	//	return;
	if(obj.header != NULL)
		free(obj.header);
	if(obj.value != NULL)
		free(obj.value);
	if(obj.cookiename != NULL)
		free(obj.cookiename);
	if(obj.cookievalue != NULL)
		free(obj.cookievalue);
	//free(obj);
}

struct http_req read_http_req(char* input_buffer, size_t readlen)
{
	struct http_req r;// = malloc(sizeof(struct http_req));
	char *tmp = input_buffer;
	r.request_type = tmp;
	while(*tmp != 0 && *tmp != ' ' && *tmp != '\r' && *tmp != '\n')
		tmp++;
	if(*tmp == ' ')
	{
		*tmp = 0;
		tmp++;
	}
	r.url = tmp;
	while(*tmp != 0 && *tmp != ' ' && *tmp != '\r' && *tmp != '\n')
		tmp++;
	if(*tmp == ' ')
	{
		*tmp = 0;
		tmp++;
	}
	r.http_type = tmp;
	while(*tmp != 0 && *tmp != ' ' && *tmp != '\r' && *tmp != '\n')
		tmp++;
	r.num_header = 0;
	r.header = malloc(sizeof(char*) * 1);
	r.value = malloc(sizeof(char*)  * 1);
	while(*tmp == '\r')
	{
		*tmp = 0;
		tmp++;
		while(*tmp != 0 && *tmp != '\n')
			tmp++;
		if(*tmp == '\n')
			tmp++;
		if(*tmp == '\r' || *tmp == 0)
		{
			while(*tmp != 0 && *tmp != '\n')
				tmp++;
			if(*tmp == '\n')
				tmp++;
			r.content = tmp;
			r.content_size = readlen - (tmp - input_buffer);
			break;
		}
		r.header = realloc(r.header, sizeof(char*) * (r.num_header + 1));
		r.header[r.num_header] = tmp;
		while(*tmp != 0 && *tmp != '\r' && *tmp != '\n' && *tmp != ':')
			tmp++;
		if(*tmp == ':')
		{
			*tmp = 0;
			tmp++;
			if(*tmp == ' ')
				tmp++;
		}
		r.value = realloc(r.value, sizeof(char*) * (r.num_header + 1));
		r.value[r.num_header] = tmp;
		while(*tmp != 0 && *tmp != '\r' && *tmp != '\n')
			tmp++;
		r.num_header++;
	}
	int j = 0;
	for(j = 0; j < r.num_header; j++)
		if(stricmp(r.header[j], "Cookie") == 0)
			break;
	r.num_cookie = 0;
	r.cookiename = NULL;
	r.cookievalue = NULL;
	if(j == r.num_header || r.value[j][0] == 0)
	{
		;
	}
	else
	{
		char* t = r.value[j];
		//printf("t is \"%s\"\n", t);
		//r.cookiename = malloc(sizeof(char*) * 1);
		//r.cookievalue = malloc(sizeof(char*) * 1);
		while(1)
		{
			r.cookiename = realloc(r.cookiename, sizeof(char*) * (r.num_cookie + 1));
			r.cookievalue = realloc(r.cookievalue, sizeof(char*) * (r.num_cookie + 1));
			r.cookiename[r.num_cookie] = t;
			while(*t != 0 && *t != ';' && *t != '=')
				t++;
			if(*t == '=')
			{
				*t = 0;
				t++;
				r.cookievalue[r.num_cookie] = t;
				r.num_cookie++;
				while(*t != 0 && *t != ';')
					t++;
				if(*t == ';')
				{
					*t = 0;
					t++;
				}
				while(*t == ' ')
					t++;
			}
			else if(*t == ';')
			{
				r.cookievalue[r.num_cookie] = NULL;
				r.num_cookie++;
				*t = 0;
				t++;
				while(*t == ' ')
					t++;
			}
			else
				break;
		}
	}
	return r;
}

char* get_http_header(struct http_req h, char* header)
{
	for(int i = 0; i < h.num_header; i++)
		if(stricmp(h.header[i], header) == 0)
			return h.value[i];
	return NULL;
}

char* get_http_cookie(struct http_req h, char* cookie)
{
	for(int i = 0; i < h.num_cookie; i++)
		if(stricmp(h.cookiename[i], cookie) == 0)
			return h.cookievalue[i];
	return NULL;
}

int serve_http_file_with_status(int fd, uint64_t clientid, FILE* f, const char* status)
{
	char msg[1024];
	int msg_size;
	if(f == NULL)
	{
		msg_size = snprintf(msg, sizeof(msg),
			"HTTP/1.1 404 Not Found\r\n"
			"Connection: keep-alive\r\n"
			"Set-Cookie: id=%" PRIu64 "; SameSite=Lax\r\n"
			"Content-Length: %zu\r\n"
			"\r\n",
			clientid, sizeof(STATIC_404) - 1);
		send(fd, msg, msg_size, 0);
		send(fd, STATIC_404, sizeof(STATIC_404) - 1, 0);
		return 1;
	}
	else
	{
		fseeko(f, 0, SEEK_END);
		off_t file_size = ftello(f);
		fseeko(f, 0, SEEK_SET);
		msg_size = snprintf(msg, sizeof(msg),
			"HTTP/1.1 %s\r\n"
			"Connection: keep-alive\r\n"
			"Set-Cookie: id=%" PRIu64 "; SameSite=Lax\r\n"
			"Content-Length: %" PRIu64 "\r\n"
			"\r\n", status, clientid, (uint64_t)file_size);
		send(fd, msg, msg_size, 0);
		while(msg_size != 0)
		{
			msg_size = fread(msg, sizeof(char), sizeof(msg), f);
			send(fd, msg, msg_size, 0);
		}
		return 1;
	}
}

int serve_http_file(int fd, uint64_t clientid, const char* file)
{
	FILE* f = fopen(file, "r");
	int i;
	if(f == NULL)
		return serve_http_file_with_status(fd, clientid, f,"404 Not Found");
	else
	{
		i = serve_http_file_with_status(fd, clientid, f,"200 OK");
		fclose(f);
		return i;
	}
}

int serve_http_directory(int fd, uint64_t clientid, const char* dir, const char* prefix)
{
	DIR* d = opendir(dir);
	if(d == NULL)
		return serve_http_file(fd, clientid, dir);
	struct dirent **entries;
	struct stat path_stat;
	char* fullpath = (char*)malloc(512);
	strcpy(fullpath, dir);
	int place = strlen(fullpath);
	if(fullpath[place - 1] != '/')
	{
		fullpath[place] = '/';
		place++;
		fullpath[place] = 0;
	}
	char* iconpath = (char*)malloc(256);
	char* modtime = (char*)malloc(256);
	char* thisline = (char*)malloc(1024);
	int is_dir = 0;
	struct tm *mod_time_tm;
	uint64_t filesize = 0;
	int files = scandir(fullpath, &entries, NULL, alphasort);
	size_t html_buf_sz = 2048 + 1024 * files;
	char *html = (char*)malloc(html_buf_sz);
	if(html == NULL)
	{
		fprintf(stderr, "failed to allocate memory for html buffer, crashing hard!!!\n");
		exit(1);
	}
	html[0] = 0;
	snprintf(html, html_buf_sz,
		"<!DOCTYPE html>"
		"<html>"
			"<head>"
				"<meta charset=\"utf-8\">"
				"<link rel=\"icon\" type=\"image/x-icon\" href=\"/favicon.ico\">"
				"<link rel=\"shortcut icon\" type=\"image/x-icon\" href=\"/favicon.ico\">"
				"<link rel=\"stylesheet\" href=\"/styles.css\" type=\"text/css\">"
				"<title>Index of %s</title>"
			"</head>"
			"<body><h1>Index of %s</h1><table><tbody>"
			"<tr>"
				"<th valign=\"top\"> </th>"
				"<th>Name</th>"
				"<th>Last modified</th>"
				"<th>Size</th>"
			"</tr>"
			"<tr><th colspan=\"4\"><hr></th></tr>",
			dir, dir);
	for(int j = 0; j < files; j++)
	{
		//printf("processing file %s\n", entries[j]->d_name);
		fullpath[place] = 0;
		strcpy(fullpath + place, entries[j]->d_name);
		stat(fullpath, &path_stat);
		is_dir = S_ISDIR(path_stat.st_mode);
		mod_time_tm = localtime( &(path_stat.st_mtime) );
		strftime(modtime, 256, "%c", mod_time_tm);
		filesize = is_dir ? 0 : path_stat.st_size;
		int extalloc = 0;
		char *ext;
		if(is_dir)
		{
			ext = (char*)malloc(sizeof("folder"));
			strcpy(ext, "folder");
			extalloc = 1;
		}
		else
		{
			ext = entries[j]->d_name + strlen(entries[j]->d_name);
			while(ext >= entries[j]->d_name && *ext != '.')
				ext--;
			if(*ext == '.')
			{
				ext++;
				snprintf(iconpath, 256, "html/icon/%s.png", ext);
				FILE* ext_check = fopen(iconpath, "r");
				if(ext_check != NULL)
				{
					fclose(ext_check);
				}
				else
				{
					ext = (char*)malloc(sizeof("unknown"));
					strcpy(ext, "unknown");
					extalloc = 1;
				}
			}
			else
			{
				ext = (char*)malloc(sizeof("unknown"));
				strcpy(ext, "unknown");
				extalloc = 1;
			}
		}
		snprintf(thisline,1024,
			"<tr>"
				"<td valign=\"top\">"
					"<img src=\"/html/icon/%s.png\" style=\"width: 32px;height: auto;\">"
				"</td>"
				"<td>"
					"<a href=\"%s%s\">%s</a>"
				"</td>"
				"<td align=\"right\">%s</td>"
				"<td align=\"right\">%" PRIu64 "</td>"
			"</tr>",
			ext, prefix, fullpath, entries[j]->d_name, modtime, filesize);
		strncat(html,thisline,html_buf_sz);
		if(extalloc)
			free(ext);
		free(entries[j]);
	}
	free(entries);
	free(iconpath);
	free(modtime);
	free(fullpath);
	closedir(d);
	strncat(html,
		"<tr><th colspan=\"4\"><hr></th></tr>"
		"</tbody></table></body></html>",html_buf_sz);
	free(thisline);
	size_t html_final = strlen(html);
	//printf(html);
	char msg[1024];
	size_t msg_size = snprintf(msg, sizeof(msg),
			"HTTP/1.1 200 OK\r\n"
			"Connection: keep-alive\r\n"
			"Set-Cookie: id=%" PRIu64 "; SameSite=Lax\r\n"
			"Content-Length: %zu\r\n"
			"Cache-Control: max-age=0\r\n"
			"\r\n", clientid, html_final);
	send(fd, msg, msg_size, 0);
	send(fd, html, html_final, 0);
	free(html);
	return 1;
}

int serve_http_cmd_output_formatted(int fd, uint64_t clientid, const char* cmd, char* const* exe_argv, int include_return)
{
	int prg_stdout[2];
	if(pipe(prg_stdout) < 0)
	{
		perror("pipe()");
		return 0;
	}
	pid_t childpid;
	if((childpid = fork()) == -1)
	{
		perror("fork()");
		close(prg_stdout[0]);
		close(prg_stdout[1]);
		return 0;
	}
	else if(childpid == 0)
	{
		// child process
		close(prg_stdout[0]);
		dup2(prg_stdout[1], STDOUT_FILENO);
		dup2(prg_stdout[1], STDERR_FILENO);
		if(afd > 0)
			close(afd);
		if(sfd > 0)
			close(sfd);
		if(h_buffer != NULL)
			free(h_buffer);
		if(a_buffer != NULL)
			free(a_buffer);
		for(int i = 0; i < MAX_CLIENTS; i++)
			if(client[i])
				close(client[i]);
		//char* const exe_argv[] = {cmd, NULL};
		char* const exe_envp[] = {
			"PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
			NULL};
		execve(cmd, exe_argv, exe_envp);
		// execve is not ever supposed to return
		perror("execve()");
		fprintf(stderr, "failed to execute command %s\n", cmd);
		return 1;
	}
	// parent process
	close(prg_stdout[1]);
	FILE *dynamic = fopen("html/dynamic.html", "r");
	fseeko(dynamic, 0, SEEK_END);
	off_t file_size = ftello(dynamic);
	fseeko(dynamic, 0, SEEK_SET);
	char *format = (char*)malloc(sizeof(char) * (file_size + 1));
	size_t realsize = fread(format, sizeof(char), file_size + 1, dynamic);
	fclose(dynamic);
	if(file_size != realsize)
		fprintf(stderr, "warn: filesize was different then expected!\n");
	format[realsize] = 0;
	size_t output_buf_sz = 65537;
	char *output = (char*)malloc(sizeof(char) * output_buf_sz);
	int status;
	printf("Waiting for child pid %d... ", childpid);
	fflush(stdout);
	waitpid(childpid, &status, 0);
	printf("Command finished with exit code %d\n", status);
	size_t output_sz = read(prg_stdout[0], output, output_buf_sz);
	output[output_sz] = 0;
	if(output_sz < 0)
	{
		perror("read()");
		fprintf(stderr, "failed to read from pipe");
	}
	else if(output_sz == 0)
	{
		fprintf(stderr,"warn: command did not produce any output!\n");
	}
	char exit_sta[64];
	if(include_return)
		if(status == 0)
			strcpy(exit_sta, "The command completed successfully.");
		else
			snprintf(exit_sta, sizeof(exit_sta), "The command failed with exit code %d.", status);
	else
		exit_sta[0] = 0;
	size_t final_sz = realsize + output_buf_sz + 65;
	char* final = (char*)malloc(sizeof(char) * final_sz);
	//printf("format looks like \"%s\", format);
	final_sz = snprintf(final, final_sz, format, exit_sta, output);
	free(output);
	free(format);
	char header[1024];
	size_t header_size = snprintf(header, sizeof(header),
		"HTTP/1.1 200 OK\r\n"
		"Connection: keep-alive\r\n"
		"Set-Cookie: id=%" PRIu64 "; SameSite=Lax\r\n"
		"Content-Length: %zu\r\n"
		"\r\n", clientid, final_sz);
	send(fd, header, header_size, 0);
	send(fd, final, final_sz, 0);
	free(final);
	return 1;
}

void sig_handler(int signo)
{
	const char sigint[] = "\ncaught SIGINT, exiting now...\n";
	const char sigterm[] = "\ncaught SIGTERM, exiting now...\n";
	if(signo == SIGINT)
		write(2, sigint, sizeof(sigint) - 1);
	if(signo == SIGTERM)
		write(2, sigterm, sizeof(sigterm) - 1);
	if(signo == SIGINT || signo == SIGTERM)
	{
		if(sfd > 0)
			close(sfd);
		if(a_buffer != NULL)
			free(a_buffer);
		if(h_buffer != NULL)
			free(h_buffer);
		for(int i = 0; i < MAX_CLIENTS; i++)
			if(client[i])
				close(client[i]);
		exit(0);
	}
}


int main(int argc, char** argv)
{
	/*if(geteuid() != 0)
	{
		fprintf(stderr, "[fail] This program must be run as root! This error can be fixed by doing any one of the following:\n");
		fprintf(stderr, "\ta) re-run this program with sudo, so something like:\n");
		fprintf(stderr, "\t\tsudo ");
		for(int i = 0; i < argc; i++)
			fprintf(stderr,"%s ",argv[i]);
		fprintf(stderr, "\n");
		fprintf(stderr, "\tb) alternatively, permanently fix this message by setting the setuid bit:\n");
		fprintf(stderr, "\t\tsudo chown root.root %s\n", argv[0]);
		fprintf(stderr, "\t\tsudo chmod 4755 %s\n", argv[0]);
		fprintf(stderr, "\t   then, rerun the program normally\n");
		fprintf(stderr, "\t\t");
		for(int i = 0; i < argc; i++)
                        fprintf(stderr,"%s ",argv[i]);
		fprintf(stderr,"\n");
		fprintf(stderr,"\nCannot possibly continue without sufficent permissions, exiting...\n");
		return 1;
	}*/
	if(argc < 2)
	{
		fprintf(stderr, "Specify a path to serve: %s <path>\n", argv[0]);
		return 1;
	}
	srand(time(0));
	if(signal(SIGINT, sig_handler) == SIG_ERR)
	{
		perror("signal()");
		fprintf(stderr, "we cant catch SIGINT, so we cant exit safely\nexiting now so we dont break anything...\n");
		return 1;
	}
	if(signal(SIGTERM, sig_handler) == SIG_ERR)
	{
		perror("signal()");
		fprintf(stderr, "we cant catch SIGTERM, so we cant exit safely\nexiting now so we dont break anything...\n");
		return 1;
	}
	size_t buffer_len = 65535;
	h_buffer = (char*)malloc(sizeof(char) * buffer_len);
	char hostname[256];
	if(gethostname(hostname, sizeof(hostname)))
	{
		perror("gethostname()");
		strcpy(hostname, "??? (unknown)");
	}
	printf("hostname is %s\n", hostname);
	struct sockaddr_in serv_addr;
	int serv_sock = socket(AF_INET, SOCK_STREAM, 0);
	memset(&serv_addr, 0, sizeof(serv_addr));
	int optval = 1;
	if(setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int))<0)
	{
		perror("setsockopt()");
		return 1;
	}
	int flags;
	if((flags = fcntl(serv_sock, F_GETFL)) == -1)
		perror("fcntl()");
	if(fcntl(serv_sock, F_SETFL, flags | O_NONBLOCK))
		perror("fcntl()");
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(8080);
	if(bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))<0)
	{
		perror("bind()");
		if(afd > 0)
			close(afd);
		return 1;
	}
	if(listen(serv_sock, 255)<0)
	{
		perror("listen()");
		if(afd > 0)
			close(afd);
		return 1;
	}
	fd_set fset;
	uint64_t clientid[MAX_CLIENTS];
	for(int i = 0; i < MAX_CLIENTS; i++)
		client[i] = 0, clientid[i] = 0;
	int max_sd, i, act;
	struct sockaddr_in address;
	socklen_t address_len = sizeof(struct sockaddr_in);
	memset(&address, 0, sizeof(struct sockaddr_in));
	int sd;
	int new_sock;
	int loop = 1;
	while(loop)
	{
		FD_ZERO(&fset);
		FD_SET(serv_sock, &fset);
		max_sd = serv_sock;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(client[i] > 2)
				FD_SET(client[i], &fset);
			if(client[i] > max_sd)
				max_sd = client[i];
		}
		act = select(max_sd + 1, &fset, NULL, NULL, NULL);
		if((act < 0) && (errno != EINTR))
			perror("select()");
		if(FD_ISSET(serv_sock, &fset))
		{
			if((new_sock = accept(serv_sock, (struct sockaddr*)&address, &address_len))<0)
			{
				perror("accept()");
				goto WHOOPS;
			}
			printf("New client at %s:%d, fd = %d\n",inet_ntoa(address.sin_addr),ntohs(address.sin_port),new_sock);
			for(i = 0; i < MAX_CLIENTS; i++)
				if(client[i] == 0)
				{
					client[i] = new_sock;
					clientid[i] = 0;
					break;
				}
		}
		WHOOPS:
		for(i = 0; i < MAX_CLIENTS; i++)
		{
			sd = client[i];
			if(sd > 0 && FD_ISSET(sd, &fset))
			{
				flags = read(sd, h_buffer, buffer_len);
				if(getpeername(sd, (struct sockaddr*)&address, &address_len)<0)
					perror("getpeername()");
				if(flags < 0)
					perror("read()");
				if(flags == 0)
				{
					printf("client at %s:%d disconnected\n",
						inet_ntoa(address.sin_addr),
						ntohs(address.sin_port));
					close(sd);
					client[i] = 0;
					clientid[i] = 0;
				}
				else
				{
					printf("====== MESSAGE FROM %s:%d =====\n",
						inet_ntoa(address.sin_addr),
						ntohs(address.sin_port));
					fwrite(h_buffer, sizeof(char), flags, stdout);
					h_buffer[flags] = 0;
					printf("\n");
					struct http_req r = read_http_req(h_buffer, flags);
					char* this_id = get_http_cookie(r, "id");
					if(this_id != NULL)
						clientid[i] = strtoull(this_id, NULL, 10);
					else
						clientid[i] = 0;
					if(clientid[i] == 0)
						while(1)
						{
							clientid[i] = rand_uint64_t();
							for(int j = 0; j < MAX_CLIENTS; j++)
								if(i != j && clientid[i] == clientid[j])
									continue;
							break;
						}
					printf("client id is %" PRIu64 "\n", clientid[i]);
					/*if(strcmp(r.url, "/") == 0)
					{
						printf("serve index.html\n");
						serve_http_file(client[i], clientid[i], "html/index.html");
					} 
					else if(strncmp(r.url, "/rootfs", 7) == 0 && r.url[7] == '/')
						serve_http_directory(client[i], clientid[i], r.url + 7, "/rootfs");
					else if(strcmp(r.url, "/snake_is_not_allowed__dont_modify_this_url__there_arent_easter_eggs_here") == 0)
						serve_http_file(client[i], clientid[i], "html/not_snek.html");
					else if(strncmp(r.url, "/snake", 6) == 0 && strstr(r.url, "not") == NULL)
						serve_http_file(client[i], clientid[i], "html/snek.html");
					else
					{
						printf("unknown file\n");
						serve_http_file(client[i], clientid[i], "html/404.html");
					}*/
					destroy_http_req(r);
				}
			}
		}
	}
	return 0;
}
