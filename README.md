# ArachnidApe #
*A very necessary bad pun to make SpiderMonkey work outside of the browser.*

## Installation ##
ArachnidApe (previously known to myself as JSShell) requires three main components:

1. A binary distribution of SpiderMonkey, for your platform. Get yours from the [Mozilla Firefox Nightly Builds page](http://ftp.mozilla.org/pub/mozilla.org/firefox/nightly/latest-trunk/).
    * scroll right to the bottom to get to the jsshell-*.zip archives.
	* Get win32 or win64-x86_64 depending on your architecture. I got mine as x64 since I'm running 64-bit Windows. It doesn't really matter, since ArachnidApe doesn't actually load the binary into its own process space.
2. A binary distribution of ArachnidApe. I may or may not build one for you... but ask nicely and I may :)
3. Stub.js. This provides you with the necessary bootstrap functions that your existing code may require.
	* Feel free to modify stub.js if you need to add more stuff. But give back, okay?
4. Explode the Mozilla SpiderMonkey zip into some folder *x*, then put the JSShell.exe binary in the same folder, and make sure Stub.js is part of all the fun.
	* Note to self: remember to use our awesome new name.
5. Fire up a command prompt, add *x* to path, and call JSShell.exe. Whatever command line options js.exe takes, JSShell.exe takes.
6. &pi;!

## Building ##
hacker(you) <=> build(source)

1. You'll need Visual Studio 2012. I was itching to try out the epic new code completion in there, so *that's why&#8482;*.
2. Get the source tarball. Or clone this. Or fork this. Whichever pleases you most.
3. Load up the solution file, and build.
4. Happiness.
