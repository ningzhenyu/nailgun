import numpy as np
from PIL import Image
import sys
import math

FINGERPRINT_START  = "--------------------Fingerprint Start"
FINGERPRINT_END    = "--------------------Fingerprint End"

if len(sys.argv) != 2:
    print("Usage:")
    print("python " + sys.argv[0] + " path_to_log_files")
    sys.exit(1)

file = sys.argv[1]

datas = []
log_data = False
with open(file) as fp:
    line = fp.readline()
    while line:
        line = fp.readline().strip()
        if line.endswith(FINGERPRINT_START):
            print("Collecting fingerprint data...")
            log_data = True
            data = []
            continue
        elif line.endswith(FINGERPRINT_END):
            if log_data:
                print("Fingerprint data has been collected!")
                log_data = False
                datas.append(data)
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
    print("Converting fingerprint " + str(index) + " to PNG file")
    nrow = int(math.sqrt(len(data)))
    img = Image.new("RGB", (nrow, nrow))
    pixels = img.load()
    for j in range(nrow):
        for i in range(nrow):
            pixel = int(data[(nrow - j - 1) * nrow + i], 16)
            pixels[i, j] = (pixel, pixel, pixel)
    img.save("fingerprint_" + str(index) + ".png")
    print("Fingerprint " + str(index) + " has been saved as fingerprint_" + str(index) + ".png")
    index += 1

print("All done, cheers!")
