import numpy as np
from PIL import Image
import sys
import math

DATA_FLAG = "--------------------"

if len(sys.argv) != 2:
    print ("wrong arg numbers")
    sys.exit(1)

file = sys.argv[1]

datas = []
log_data = False
with open(file) as fp:
    line = fp.readline()
    while line:
        line = fp.readline().strip()
        if line.endswith(DATA_FLAG):
            if log_data:
                log_data = False
                datas.append(data)
            else:
                log_data = True
                data = []
                continue
        if log_data:
            line = line.split(':')[1]
            image_data = line.strip().split(' ')
            for d in image_data:
                data.append(d[6:8])
                data.append(d[4:6])
                data.append(d[2:4])
                data.append(d[0:2])

index = 0
for data in datas:
    nrow = int(math.sqrt(len(data)))
    img = Image.new('RGB', (nrow, nrow))
    pixels = img.load()
    for j in range(nrow):
        for i in range(nrow):
            pixel = int(data[(nrow - j - 1) * nrow + i], 16)
            pixels[i, j] = (pixel, pixel, pixel)
    img.save('fingerprint_' + str(index) + '.png')
    index += 1