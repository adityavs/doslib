@echo off
rem ----- Auto-generated by make.sh and Linux make system do not modify

rem ----- shut up DOS4G/W
set DOS4G=quiet

if "%1" == "clean" call clean.bat
if "%1" == "clean" goto end

mkdir dos86c
wmake -f ..\..\mak\dos86c.mak HPS=\ all REL=..\.. 

mkdir dos86l
wmake -f ..\..\mak\dos86l.mak HPS=\ all REL=..\.. 

mkdir dos86m
wmake -f ..\..\mak\dos86m.mak HPS=\ all REL=..\.. 

mkdir dos86s
wmake -f ..\..\mak\dos86s.mak HPS=\ all REL=..\.. 

mkdir dos386f
wmake -f ..\..\mak\dos386f.mak HPS=\ all REL=..\.. 

mkdir os2w2l
wmake -f ..\..\mak\os2w2l.mak HPS=\ all REL=..\.. 

mkdir os2d3f
wmake -f ..\..\mak\os2d3f.mak HPS=\ all REL=..\.. 

mkdir win302l
wmake -f ..\..\mak\win302l.mak HPS=\ all REL=..\.. 

mkdir win313l
wmake -f ..\..\mak\win313l.mak HPS=\ all REL=..\.. 

mkdir win32s3
wmake -f ..\..\mak\win32s3.mak HPS=\ all REL=..\.. 

mkdir winnt
wmake -f ..\..\mak\winnt.mak HPS=\ all REL=..\.. 

mkdir win32
wmake -f ..\..\mak\win32.mak HPS=\ all REL=..\.. 

mkdir win386
wmake -f ..\..\mak\win386.mak HPS=\ all REL=..\.. 

mkdir win38631
wmake -f ..\..\mak\win38631.mak HPS=\ all REL=..\.. 


:end
