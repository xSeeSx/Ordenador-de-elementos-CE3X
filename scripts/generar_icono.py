"""Generate multi-resolution Windows icon, without third-party dependencies."""
from pathlib import Path
import struct
import sys

SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
BLUE = (58, 69, 154, 255)
WHITE = (255, 255, 255, 255)
ICE = (183, 217, 255, 255)
CLEAR = (0, 0, 0, 0)


def rounded(x, y, left, top, width, height, radius):
    if not (left <= x <= left+width and top <= y <= top+height):
        return False
    cx = max(left+radius, min(x, left+width-radius))
    cy = max(top+radius, min(y, top+height-radius))
    return (x-cx)**2+(y-cy)**2 <= radius**2


SEGMENTS = ((89,49,89,29), (80,38,89,29), (89,29,98,38),
            (89,78,89,98), (80,89,89,98), (89,98,98,89))


def segment(x,y,a,b,c,d):
    dx,dy=c-a,d-b
    t=max(0,min(1,((x-a)*dx+(y-b)*dy)/(dx*dx+dy*dy)))
    return (x-a-t*dx)**2+(y-b-t*dy)**2 <= 3.5**2


def color(x,y):
    if not rounded(x,y,4,4,120,120,25):
        return CLEAR
    if any(rounded(x,y,24,t,w,13,4) for t,w in ((30,51),(57,39),(84,51))):
        return WHITE
    if 75 <= x <= 103 and 24 <= y <= 103 and any(segment(x,y,*s) for s in SEGMENTS):
        return ICE
    return BLUE


def bitmap(size):
    # Four samples per pixel, with alpha-weighted colors at transparent edges.
    pixels=bytearray()
    stride=((size+31)//32)*4
    mask=bytearray(stride*size)
    for y in range(size-1,-1,-1):
        for x in range(size):
            samples=[color((x+dx)*128/size,(y+dy)*128/size)
                     for dx in (.25,.75) for dy in (.25,.75)]
            opaque=[p for p in samples if p[3]]
            n=len(opaque)
            if n:
                r,g,b=(round(sum(p[i] for p in opaque)/n) for i in range(3))
                pixels.extend((b,g,r,round(n*255/4)))
            else:
                pixels.extend((0,0,0,0))
                mask[(size-1-y)*stride+x//8] |= 128>>(x%8)
    header=struct.pack('<IiiHHIIiiII',40,size,size*2,1,32,0,len(pixels),0,0,0,0)
    return header+pixels+mask


def ico():
    entries=[]
    payload=[]
    offset=6+16*len(SIZES)
    for size in SIZES:
        data=bitmap(size)
        edge=size if size<256 else 0
        entries.append(struct.pack('<BBBBHHII',edge,edge,0,0,1,32,len(data),offset))
        payload.append(data)
        offset+=len(data)
    return struct.pack('<HHH',0,1,len(SIZES))+b''.join(entries+payload)


if __name__=='__main__':
    Path(sys.argv[1]).write_bytes(ico())
