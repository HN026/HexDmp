binary:
	@mkdir -p bin
	gcc -o bin/dmp src/dmp.c src/args.c