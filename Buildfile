flags=-Os
cxx=g++

build {
    TARGET=arg[2]
    SOURCE=main.cpp
    {cxx} -o {TARGET} {SOURCE} {flags}
}
buildforce {
    TARGET=arg[2]
    SOURCE=main.cpp
    FORCERUN=true
    {cxx} -o {TARGET} {SOURCE} {flags}
}