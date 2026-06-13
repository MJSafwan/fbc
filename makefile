main : main.c arena.h
	clang -g -I/Users/hoods/Documents/etc/lineedit/include -L /Users/hoods/Documents/etc/lineedit/lib/ -leditline -o main main.c
