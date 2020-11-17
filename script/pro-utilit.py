#!/usr/bin/env python
import os
import argparse
def mkdir(path):
    folder = os.path.exists(path)
    if not folder:    
        os.makedirs(path)
def ReafByteInfo(fileName):
    bytes=0
    for index, line in enumerate(open(fileName,'r')):
        lineArr = line.strip().split()
        bytes=bytes+int(lineArr[3])
    return bytes
def CoutByteForFlows(ins,algo,flows):
    name=str(ins)+"_"+algo+"_"+"%s"+"_owd.txt"
    bytes=0;
    for i in range(flows):
        filename=name%str(i+1)
        if os.path.exists(filename):
            bytes=bytes+ReafByteInfo(filename)
        else:
            bytes=0
            break
    return bytes
parser = argparse.ArgumentParser(description='manual to this script')
parser.add_argument('--algo', type=str, default ='olia')
args = parser.parse_args()
algo=args.algo
name="%s_util.txt"
instance=[1,2,3,4,5,6,7,8]
flows=4
data_dir="data_process"
out_path=data_dir+"/"
mkdir(out_path)
fileout=name%algo
caps=[5000000,5000000,5000000,5000000,6000000,6000000,8000000,8000000]
duration=300;
fout=open(out_path+fileout,'w')
for case in range(len(instance)):
    bytes=0
    total=caps[instance[case]-1]*duration/8
    bytes=CoutByteForFlows(instance[case],algo,flows)
    util=float(bytes)/float(total)
    fout.write(str(instance[case])+"\t")
    fout.write(str(util)+"\n")
fout.close()
