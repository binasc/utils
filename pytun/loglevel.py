import sys
import logging

formatter = logging.Formatter(fmt='%(asctime)-15s %(levelname)-5s %(name)s %(message)s')
handler = logging.StreamHandler(sys.stdout)
handler.setFormatter(formatter)

gLevel = logging.DEBUG
logging.basicConfig(fmt=formatter)

def getLogger(name, level=gLevel):
    logger = logging.getLogger(name)
    logger.level = level
    if not logger.handlers:
        logger.addHandler(handler)
    logger.propagate = False
    return logger
