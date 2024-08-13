#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdio.h>

volatile sig_atomic_t continue_receiving = 1;

/// @brief Signal handler.
/// @details Atomically change the flag for graceful shutdown.
/// @param signo Incoming signal number.
static void SignalHandler(int signo)
{
    (void)signo;
    continue_receiving = 0;
}

/// @brief Read stream from @c fd_from and write it into @c fd_to.
/// @param fd_from File descriptor to read stream from.
/// @param fd_to File descriptor to write stream into.
/// @return 0 if there's no error, the number of errno otherwise.
/// @pre @c fd_from is non-negative, and for non-blocking I/O.
/// @pre @c fd_to is non-negative, and for non-blocking I/O.
/// @post On success, the content of the packet is written to @c text.
/// @post On error, @c syslog is invoked with an appropriate message.
static int TransferFromFdToFd(const int fd_from, const int fd_to)
{
    assert(0 <= fd_from);
    assert(0 <= fd_to);
    const size_t bufsize = 100;
    char buf[bufsize];
    bool stream_started = false;
    while (true)
    {
        ssize_t readsize = read(fd_from, buf, bufsize);
        if (readsize == -1)
        {
            int err = errno;
            if (err == EAGAIN || err == EWOULDBLOCK)
            {
                if (stream_started)
                {
                    // Reached EOF
                    return 0;
                }
                else
                {
                    // The message hasn't arrived yet.
                    continue;
                }
            }
            syslog(LOG_ERR, "Failed to read the data, error: %s", strerror(err));
            return err;
        }
        if (readsize == 0)
        {
            // EOF
            return 0;
        }

        stream_started = true;
        size_t write_remaining = (size_t)readsize;
        while (0 < write_remaining)
        {
            size_t offset = (size_t)readsize - write_remaining;
            ssize_t written = write(fd_to, (const char *)buf + offset, write_remaining);
            if (written == -1)
            {
                int err = errno;
                syslog(LOG_ERR, "Failed to write the data, error: %s", strerror(err));
                return err;
            }
            write_remaining -= (size_t)written;
        }
    }
    assert(false); // Unreachable
}

/// @brief Receive the entire packet and write it into the text file.
/// @param sockfd File descriptor for socket.
/// @param testfd File descriptor for the text.
/// @return 0 if there's no error, the number of errno otherwise.
/// @pre @c sockfd is non-negative, and for non-blocking I/O.
/// @pre @c textfd is non-negative, and for non-blocking I/O.
/// @post On success, the content of the packet is written to @c text.
/// @post On error, @c syslog is invoked with an appropriate message.
static int RecvAllToFile(const int sockfd, const int textfd)
{
    return TransferFromFdToFd(sockfd, textfd);
}

/// @brief Send the entire content of a text file to the specified socket.
/// @param sockfd File descriptor for socket.
/// @param text_path Path of the text.
/// @return 0 if there's no error, the number of errno otherwise.
/// @pre @c sockfd is non-negative.
/// @pre @c text_path is not @c NULL.
/// @post On success, the content of the file is sent.
/// @post On error, @c syslog is invoked with an appropriate message.
static int SendAllFromFile(const int sockfd, const char *const text_path)
{
    assert(0 <= sockfd);
    assert(text_path != NULL);
    int ret = 0;

    int fd = open(text_path, O_RDONLY | O_NONBLOCK);
    if (fd == -1)
    {
        ret = errno;
        syslog(LOG_ERR, "Failed to open the text file for reading, error: %s", strerror(ret));
        return ret;
    }
    ret = TransferFromFdToFd(fd, sockfd);
    assert(fd != -1);
    close(fd);
    return ret;
}

/// @brief stores the values each of which needs a dedicated clean-up after
struct ValuesToBeCleanedUp
{
    /// @brief Socket file descriptor for server.
    int server_sockfd;
    /// @brief File descriptor for appending the data to the text.
    int textfd;
    /// @brief Socket file descriptor for client.
    int client_sockfd;
};

/// @brief Text path string
const char *const textPath = "/var/tmp/aesdsocketdata";

/// @brief Implementation of @c main() without set-up or clean-up.
/// @param vals Pointer to values each of which needs a clean-up after executing this function.
/// @return The return value for @c main().
/// @pre @c vals is not @c NULL.
/// @post If it exits on error, @c syslog describing the error is called.
static int RunMain(struct ValuesToBeCleanedUp *const vals)
{
    assert(vals != NULL);
    const int ret_error = -1;

    vals->server_sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (vals->server_sockfd == -1)
    {
        syslog(LOG_ERR, "Failed to create server_sockfd, error: %s", strerror(errno));
        return ret_error;
    }
    vals->textfd = open(textPath, O_WRONLY | O_CREAT | O_APPEND | O_NONBLOCK, 0644);
    if (vals->textfd == -1)
    {
        syslog(LOG_ERR, "Failed to open the text file for appending, error: %s", strerror(errno));
        return ret_error;
    }

    const int enabled = 1;
    if (setsockopt(vals->server_sockfd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) == -1)
    {
        syslog(LOG_ERR, "Failed to set sockopt, error: %s", strerror(errno));
        return ret_error;
    }

    struct sockaddr_in server_addr;
    (void)memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9000);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(vals->server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        syslog(LOG_ERR, "Failed to bind, error: %s", strerror(errno));
        return ret_error;
    }

    // TODO: fork here

    if (listen(vals->server_sockfd, 16) == -1)
    {
        syslog(LOG_ERR, "Failed to listen, error: %s", strerror(errno));
        return ret_error;
    }

    if (signal(SIGINT, SignalHandler) == SIG_ERR)
    {
        syslog(LOG_ERR, "Failed to set hander for SIGINT, error: %s", strerror(errno));
        return ret_error;
    }
    if (signal(SIGTERM, SignalHandler) == SIG_ERR)
    {
        syslog(LOG_ERR, "Failed to set hander for SIGTERM, error: %s", strerror(errno));
        return ret_error;
    }

    struct sockaddr_in client_addr;

    while (continue_receiving == 1)
    {
        (void)memset(&client_addr, 0, sizeof(client_addr));

        socklen_t client_len = sizeof(client_addr);
        vals->client_sockfd = accept(vals->server_sockfd, (struct sockaddr *)&client_addr, &client_len);
        if (vals->client_sockfd == -1)
        {
            int err = errno;
            if (err == EAGAIN || err == EWOULDBLOCK)
            {
                // The message hasn't arrived yet.
                continue;
            }
            syslog(LOG_ERR, "Failed to accept, error: %s", strerror(err));
            return ret_error;
        }
        // TODO: is it needed to set client flags?
        int client_flags = fcntl(vals->client_sockfd, F_GETFL, 0);
        if (client_flags == -1)
        {
            syslog(LOG_ERR, "Failed to get client flags, error: %s", strerror(errno));
            return ret_error;
        }

        // Set the socket to non-blocking mode
        client_flags = fcntl(vals->client_sockfd, F_SETFL, client_flags | O_NONBLOCK);
        if (client_flags == -1)
        {
            syslog(LOG_ERR, "Failed to set client flags, error: %s", strerror(errno));
            return ret_error;
        }

        if (RecvAllToFile(vals->client_sockfd, vals->textfd) != 0)
        {
            return ret_error;
        }

        if (SendAllFromFile(vals->client_sockfd, textPath) != 0)
        {
            return ret_error;
        }

        if (close(vals->client_sockfd) == -1)
        {
            syslog(LOG_ERR, "Failed to close the socket fd, error: %s", strerror(errno));
            return ret_error;
        }
        else
        {
            vals->client_sockfd = -1;
            char ip_as_str[INET_ADDRSTRLEN + 1];
            (void)memset(&ip_as_str, 0, sizeof(ip_as_str));
            if (inet_ntop(AF_INET, &client_addr.sin_addr, ip_as_str, INET_ADDRSTRLEN) == NULL)
            {
                syslog(LOG_ERR, "Failed to get the IP string, error: %s", strerror(errno));
                return ret_error;
            }

            syslog(LOG_INFO, "Closed connection from %s", ip_as_str);
        }
    }
    syslog(LOG_INFO, "Caught signal, exiting");
    return 0;
}

int main()
{
    struct ValuesToBeCleanedUp vals;
    vals.client_sockfd = -1;
    vals.server_sockfd = -1;
    vals.textfd = -1;

    int return_val = RunMain(&vals);

    if (vals.textfd != -1)
    {
        (void)close(vals.textfd);
        (void)unlink(textPath);
    }
    if (vals.client_sockfd != -1)
    {
        (void)close(vals.client_sockfd);
    }
    if (vals.server_sockfd != -1)
    {
        (void)close(vals.server_sockfd);
    }

    return return_val;
}
