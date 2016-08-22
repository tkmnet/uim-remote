#include <sys/socket.h>
#include <sys/param.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <regex.h>

#include <uim/uim.h>
#include <uim/uim-helper.h>

#ifndef INFTIM
 #define INFTIM -1
#endif


static int uim_fd;
char* activeim;
char* inactiveim;
int currentMode = 0;

static ssize_t xwrite(int fd, const char *buf, size_t size);
const char* send_message(const char* msg);
void prop_list_get();

static ssize_t xwrite(int fd, const char *buf, size_t size)
{
    ssize_t n;
    size_t s = 0;
    while (s < size)
    {
        do {
            n = write(uim_fd, buf + s, size - s);
        } while (n == -1 && errno == EINTR);
        if (n == -1)
        { return -1; }
        s += n;
    }
    return s;
}

const char*
send_message(const char* msg)
{
  if (uim_fd == 0)
  { return NULL; }
  xwrite(uim_fd, msg, strlen(msg));
  xwrite(uim_fd, "\n", 1);
  return NULL;
}

void
prop_list_get()
{
    char tmp[BUFSIZ];
    char *buf = strdup("");
    char *p;
    struct pollfd pfd;
    ssize_t n;
    char end_flag = 1;

    send_message("prop_list_get\n");

    pfd.fd = uim_fd;
    pfd.events = (POLLIN | POLLPRI);

    while (end_flag)
    {
        do {
            n = poll(&pfd, 1, INFTIM);
        } while (n == -1 && errno == EINTR);
        if (n == -1)
        { break; }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
        { break; }
        do {
            n = read(uim_fd, tmp, sizeof(tmp));
        } while (n == -1 && errno == EINTR);
        if (n == 0 || n == -1)
        { break; }
        if (tmp[0] == 0)
        { continue; }
        buf = uim_helper_buffer_append(buf, tmp, n);
        while ((p = uim_helper_buffer_get_message(buf)) != NULL)
        {
            if (strncmp(p, "prop_list_update", sizeof("prop_list_update") - 1) == 0)
            {
                end_flag = 0;
                break;
            }
        }
    }

    regex_t regexBuffer;

    if( regcomp( &regexBuffer, "(action_imsw_[a-zA-Z0-9]+)\t[*]", REG_EXTENDED | REG_NEWLINE ) != 0 )
    {
        puts("regex compile failed\n");
        return;
    }

    regmatch_t patternMatch[4];

    int size = sizeof( patternMatch ) / sizeof( regmatch_t );

    //    if( regexec( &regexBuffer, checkString, size, patternMatch, 0 ) != 0 )
    if( regexec( &regexBuffer, p, size, patternMatch, 0 ) != 0 )
    {
        regfree( &regexBuffer );
        //            puts("No match!!\n");

        if (currentMode == 0)
        {
            if( strstr(p, "ひらがな") )
            { currentMode = 2; }
            else
            { currentMode = 1; }
        }

        return;
    }

    for( int i = 0; i < size; ++i )
    {
        int startIndex = patternMatch[i].rm_so;
        int endIndex = patternMatch[i].rm_eo;
        if( startIndex == -1 || endIndex == -1 )
        {
            puts("exit\n");
            continue;
        }

        int charCount = endIndex - startIndex;
        strncpy(tmp, p + startIndex, charCount);
        tmp[charCount] = '\0';

        //printf("%s\n", tmp +12);

        if ((0 == strcmp("direct\t*", tmp +12)) || (0 == strcmp("latin", tmp +12)))
        { currentMode = 1; }
        else
        { currentMode = 2; }

        break;
    }

    regfree( &regexBuffer );

//    char checkString* = p;

    if( regcomp( &regexBuffer, "(action_imsw_[a-zA-Z0-9]+)", REG_EXTENDED | REG_NEWLINE ) != 0 )
    {
        puts("regex compile failed\n");
        return;
    }

    size = sizeof( patternMatch ) / sizeof( regmatch_t );

    for (;;)
    {
        //    if( regexec( &regexBuffer, checkString, size, patternMatch, 0 ) != 0 )
        if( regexec( &regexBuffer, p, size, patternMatch, 0 ) != 0 )
        {
            regfree( &regexBuffer );
//            puts("No match!!\n");
            return;
        }

        for( int i = 0; i < size; ++i )
        {
            int startIndex = patternMatch[i].rm_so;
            int endIndex = patternMatch[i].rm_eo;
            if( startIndex == -1 || endIndex == -1 )
            {
                puts("exit\n");
                continue;
            }

            int charCount = endIndex - startIndex;
            strncpy(tmp, p + startIndex, charCount);
            tmp[charCount] = '\0';

//            printf("%s\n", tmp);

            if ((0 == strcmp("direct", tmp +12)) || (0 == strcmp("latin", tmp +12)))
            {
                inactiveim = (char*)malloc(charCount +1);
                strcpy(inactiveim, tmp);
            }
            else
            {
                activeim = (char*)malloc(charCount +1);
                strcpy(activeim, tmp);
            }

            p = p + endIndex;
            break;
        }
    }

    regfree( &regexBuffer );

    return;
}

int main(int argc, char *argv[])
{
    struct sockaddr_un server;
    char path[MAXPATHLEN];

    if (uim_fd != 0)
    { return -1; }

    if (!uim_helper_get_pathname(path, sizeof(path)))
    { return -2; /* "error uim_helper_get_pathname()" */ }

    bzero(&server, sizeof(server));
    server.sun_family = PF_UNIX;
    do {
        strncpy(server.sun_path, path, sizeof(server.sun_path));
        server.sun_path[sizeof(server.sun_path) - 1] = '\0';
    } while (0);

    uim_fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (uim_fd < 0)
    { return -3; /* "error socket()"; */ }

    if (connect(uim_fd, (struct sockaddr *)&server, sizeof(server)) != 0)
    {
        shutdown(uim_fd, SHUT_RDWR);
        close(uim_fd);
        return -4; /* "cannot connect to uim-helper-server" */
    }

    if (uim_helper_check_connection_fd(uim_fd) != 0)
    {
        shutdown(uim_fd, SHUT_RDWR);
        close(uim_fd);
        return -5; /* "error uim_helper_check_connection_fd()" */
    }

    //----------

    prop_list_get();

    if (argc <= 1)
    {
        printf("%d\n", currentMode);
    }
    else
    {
        if (argv[1][0] == '-' && argv[1][1] != '\0')
        {
            char msg[64];
            switch (argv[1][1])
            {
                case 'c':
                    sprintf(msg, "prop_activate\n%s\n", inactiveim);
                    send_message(msg);
                    break;
                case 'o':
                    sprintf(msg, "prop_activate\n%s\n", activeim);
                    send_message(msg);
                    break;
                case 't':
                case 'T':
                    if (currentMode == 1)
                    { sprintf(msg, "prop_activate\n%s\n", activeim); }
                    else
                    { sprintf(msg, "prop_activate\n%s\n", inactiveim); }
                    send_message(msg);
                    break;
                case 'r':
                    send_message("prop_activate\naction_mozc_reconvert\n");
                    break;
                case 'h':
                    puts("Usage: uim-remort [OPTION]\n\t-c\t\tinactivate input method\n\t-o\t\tactivate input method\n\t-t,-T\t\tswitch Active/Inactive\n\t-r\t\tsend mozc reconvert action\n\t[no option]\t1 for inactive, 2 for active\n\t-h\t\tdisplay this help and exit");
                    break;
            }
        }
    }

    //----------

    if (uim_fd == 0)
    { return 1; }

    shutdown(uim_fd, SHUT_RDWR);
    close(uim_fd);
    return 0;
}
