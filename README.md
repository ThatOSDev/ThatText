# ThatText  
An stb_truetype OpenGL 4.3 text-rendering demonstration. NO FREETYPE LIBRARY REQUIRED !  
  
Tested with GCC 13.2 (MinGW x64) on windows.  
  
## NOTE
This example shows how to use nothing but [stb_truetype](http://nothings.org) along with [GLAD2](https://gen.glad.sh) libraries and nothing else required to render text.  
  
[GLFW3](https://www.glfw.org) is used to make the window. Nothing is dependant on GLFW3 library. So SDL2 could be used instead, and just replace GLFW3 functions if desired.  
  
  
## Screenshot  
![screenshot example](/screenshots/screenshot.png)
  
## Source Code -- Using Codeblocks on Windows 10  
NOTE : MAKE SURE TO DEFINE THIS :   
```c
_GLFW_WIN32
```  
Library to add when on Windows is gdi32 : -lgdi32  
  
  
![Source Files](/screenshots/sourcefiles.png)
  

