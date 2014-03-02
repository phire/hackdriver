
test: test.cpp mailbox.o
	g++ -o test -Wall -Wextra test.cpp mailbox.o -g

mailbox.o: mailbox.c mailbox.h
	g++ -c -o mailbox.o -Wall -Wextra mailbox.c -g

