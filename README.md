# parmake

A multithreaded implementation of make.

![Application Image](parmake.png)

## Building

Prerequisites
- GCC
- Clang
- Make

```bash
sudo apt-get update && sudo apt-get install clang-5.0 libc++abi-dev libc++-dev git gdb valgrind graphviz imagemagick gnuplot
sudo apt-get install libncurses5-dev libncursesw5-dev
git clone https://github.com/realeigenvalue/parmake.git
cd parmake
make
parmake [ -f makefile ] [ -j threads ] [ targets ]
```

## Contributing
Pull requests are welcome. For major changes, please open an issue first to discuss what you would like to change.

Please make sure to update tests as appropriate.

## License
GNU GPLv3
