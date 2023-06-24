# Simple HTTP server in C

Compile with <code>make</code>, or build a debug version with <code>make server_debug</code> (for use with <code>gdb</code> or <code>valgrind</code>).

## Usage

Specify the files to share, so something like:

<code>./server /path/to/www</code>

The server runs on port 8080 by default.  Connect to it by typing

<code>http://</code><i>your IP address</i><code>:8080/</code>

into your web browser.
