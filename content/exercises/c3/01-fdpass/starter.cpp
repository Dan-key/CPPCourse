#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

// Передача ДЕСКРИПТОРА между процессами через UNIX-сокет (SCM_RIGHTS) —
// «суперсила» доменных сокетов: получатель получает НОВЫЙ номер fd, ссылающийся
// на ТО ЖЕ открытое описание файла (OFD), что у отправителя. Так раздают
// соединения воркерам, передают memfd и т.п.
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog

// Отправить дескриптор fd по доменному сокету sock (через ancillary-данные
// SCM_RIGHTS). Нужно отправить хотя бы 1 байт обычных данных вместе с ним.
// Вернуть 0 при успехе, -1 при ошибке.
//
// Скелет:
//   char dummy = 'x';
//   struct iovec iov{ &dummy, 1 };
//   char cbuf[CMSG_SPACE(sizeof(int))]; memset(cbuf, 0, sizeof cbuf);
//   struct msghdr msg{}; msg.msg_iov=&iov; msg.msg_iovlen=1;
//   msg.msg_control=cbuf; msg.msg_controllen=sizeof cbuf;
//   struct cmsghdr* cm = CMSG_FIRSTHDR(&msg);
//   cm->cmsg_level=SOL_SOCKET; cm->cmsg_type=SCM_RIGHTS; cm->cmsg_len=CMSG_LEN(sizeof(int));
//   memcpy(CMSG_DATA(cm), &fd, sizeof(int));
//   return sendmsg(sock, &msg, 0) >= 0 ? 0 : -1;
int send_fd(int sock, int fd) {
    (void)sock; (void)fd;
    return -1; // TODO
}

// Принять дескриптор с доменного сокета sock. Вернуть НОВЫЙ fd (>=0) или -1.
//
// Скелет:
//   char dummy; struct iovec iov{ &dummy, 1 };
//   char cbuf[CMSG_SPACE(sizeof(int))];
//   struct msghdr msg{}; msg.msg_iov=&iov; msg.msg_iovlen=1;
//   msg.msg_control=cbuf; msg.msg_controllen=sizeof cbuf;
//   if (recvmsg(sock, &msg, 0) < 0) return -1;
//   struct cmsghdr* cm = CMSG_FIRSTHDR(&msg);
//   if (!cm || cm->cmsg_type != SCM_RIGHTS) return -1;
//   int fd; memcpy(&fd, CMSG_DATA(cm), sizeof(int)); return fd;
int recv_fd(int sock) {
    (void)sock;
    return -1; // TODO
}
