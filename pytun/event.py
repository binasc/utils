
class Event:

    __fd = 0
    __write = False
    __handler = None

    def setFd(self, fd):
        self.__fd = fd

    def getFd(self):
        return self.__fd

    def isWrite(self):
        return self.__write

    def setWrite(self, write):
        self.__write = write

    def getHandler(self):
        return self.__handler

    def setHandler(self, handler):
        self.__handler = handler

