import os
import argparse
def mkdir(path):
    folder = os.path.exists(path)
    if not folder:    
        os.makedirs(path)
def ReafByteInfo(fileName,left,right):
    bytes=0
    if os.path.exists(fileName):
        for index, line in enumerate(open(fileName,'r')):
            lineArr = line.strip().split()
            time=float(lineArr[0])
            if time>right:
                break
            if time>=left:
                bytes=bytes+int(lineArr[3])
    return bytes

parser = argparse.ArgumentParser(description='manual to this script')
parser.add_argument('--algo', type=str, default ='olia')
args = parser.parse_args()
algo=args.algo
data_dir="data_process"
out_path=data_dir+"/"
fileName1="%s_"+algo+"_1_owd.txt"
fileName2="%s_"+algo+"_4_owd.txt"
fileOutName="mp_"+algo+"_bw"
duration=300.0
gap=5.0
total=int(duration/gap);
instance=[3,4]
mkdir(out_path)
for case in range(len(instance)):
    fileOut=fileOutName+"_"+str(instance[case])+".txt"
    fout=open(out_path+fileOut,'w')
    for i in range(total):
        left=i*gap
        right=(i+1)*gap
        f1=fileName1%(str(instance[case]))
        f2=fileName2%(str(instance[case]))
        bytes1=ReafByteInfo(f1,left,right)
        bytes2=ReafByteInfo(f2,left,right)
        rate=(bytes1+bytes2)*8/(gap*1000)
        fout.write(str(right)+"\t"+str(rate)+"\n")
    fout.close()
