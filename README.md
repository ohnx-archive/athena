# athena
## what is athena?
Athena is an IRC bot written in C.
I used it to try and understand networking and processes in C.
There are two main parts to it: the web server and the IRC bot.
Feel free to poke around the code.
I have tried to add as many useful comments where I know what's going on.
## installation
**athena only supports linux!** I don't know if it can compile on other things (eg Windows/Mac OS), but you sure can try. I won't be able to help, though.

Try running ```git clone https://github.com/ohnx/athena.git```, making he config changes in athenabot.c, then running ```gcc athenabot.c -o athenabot```.
If there are any errors, then please create an issue with the compilation error, or try to fix it yourself. I have included a few comments here and there that hopefully explain what thing do.
## help
Run the command ```help``` if you want help. Try a command out if you don't know what it does, but for the most part, it shouldn't be too difficult to guess what a command does.

Naturally, admin commands can only be run by the owner (specified in the configuration ath the top of the file).
