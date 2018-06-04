# read-multi-stream

## Introduction

A C++11 prototype application exploring the use of the Linux `select()` system call and `read()` system call, invoked in a non-blocking manner, to process multiple input file streams concurrently.

## Description of program project

The source code is C++11 compliant and has been compiled and successfully executed where used the GNU C++ (g++) 4.8.4 compiler. The program was developed in the JetBrains CLion C++ IDE and is therefore a cmake-based project.

## Description of program operation

The program will process one or more file paths as specified on its command line when invoked. It assumes that each file will be a text file compressed using gzip and thus the file names are expected to end in the `'.gz'` suffix. The program will, for each file, perform a `fork()` system call and then `exec` the `gzip` program (which is assumed can be locatable via the `PATH` environment variable), it will setup redirection of a `gzip` child process `stdout` and `stderr` so that these pipe streams can be processed as input streams by the program's parent process.

The C++ class `read_multi_stream` is used to manage the redirected streams of each forked child process. The method `read_multi_stream::wait_for_io()` is used to wait for i/o activity on these redirected pipes. It will return with a vector populated with any pipe file descriptors that are ready to be read. The program dispatches these active file descriptors to be read via an asynchronously invoked read processing function and then waits on futures of all dispatched file descriptors. When all futures are harvested, then it iterates and calls `read_multi_stream::wait_for_io()` again, repeating the cycle until all pipes have been read to end-of-file condition (or errored out).

The output of the pipes, assumed to be text streams, will be read a text line at a time. The program will write the output of a redirected `stdout` to a file by the same name as the input file, but omitting the `'.gz'` suffix. The redirected `stderr` is written to a file by the same name as this output file but with the suffix `'.err'` appended - any diagnostic output or errors occurring per the processing of a given input file will be written to its corresponding `'.err'` file.

The operation to process a given text line is dealt with as a lambda callable; the current implementation merely writes the text line to the destination output file, however, this lambda callable is where application logic processing could be performed (if any) on each text line at a time.

The C++11 `std::async()` function is used to asychronously process each ready-to-read file descriptor, where the `std::launch::async` option is used to insure is processed on some thread.

GNU g++ 4.8.4 appears to map asychronous invocation directly to pthread library threads. The C++11 standard did not dictate an implementation approach for `std::async()` so it is conceivable that an implementor might utilize a sophisticated thread pool incorporating work stealing algorithms, etc. Future versions of C++ - probably starting at C++20 - will perhaps introduce exectutors and thread pools with richer APIs.

The GNU g++ 4.8.4 implementation of `std::async(std::launch::async, ...)` does exhibit interleaved, round robin concurrency behavior, though, when this program is fed lots of input files to process.

Keep in mind, the nature of the way this program works was devised so as to test out and explore programming with the `select()` system call, in conjunction to non-blocking invocation of the `read()` system call, in conjunction to C++11 asynchronous and concurrency (futures) capabilities. It is not intended as necessarily the ideal means to mass gzip decompress large numbers of files, none-the-less, it's execution at doing so is rather brisk.

**NOTE:** *The program does tend to write a lot of debug messages (it's an experimental prototype - not production code) so is a good idea to redirect its `stderr` output to a file, or even to `2>/dev/null` in order to see the program execute a closer to optimal speed.*

## The bigger picture

The greater intent of this exploration is to devise a particular reactive programming implementation that will be infused into another github project:

[**Spartan - a "forking" java program launcher**](https://github.com/tideworks/spartan-launcher)

**Spartan brief description:**

*Java supervisor process can launch Java child worker processes (uses Linux fork and then instantiates JVM). Child programs run as same user identity/permissions capability - no authentication or special permissioning necessary. A Spartan-launched Java program thus shares the same Java code amongst parent supervisor and children processes - just as with using `fork()` in C and C++ programming.*