# HEXDMP

This is a simple command-line utility for dumping the contents of a file in hexadecimal format along with ASCII representation.

## Usage

```bash
$ git clone 
$ make
$ ./bin/dmp [filename]
```

## Arguments
- `[file]`: File to read (default: STDIN).

## Options & Flags

- `-l, --line <int>`: Bytes per line in output (default: 16).
  ```bash
    $ ./bin/dmp -l <int> [filename]
  ```
- `-n, --num <int>`: Number of bytes to read (default: all).
  ```bash
    $ ./bin/dmp -n <int> [filename]
  ```
- `-o, --offset <int>`: Byte offset at which to begin reading.
  ```bash
    $ ./bin/dmp -o <int> [filename]
  ```
- `-h, --help`: Display help text and exit.
- `-v, --version`: Display version number and exit.


# Example
![Sample Output](sample.png)


## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

Built by [Huzaifa](https://github.com/HN026)  
Connect with me on [LinkedIn](https://www.linkedin.com/in/huzaifanaseer/)
