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
fileName1="%s_"+algo+"_1_owd.txt"
fileName2="%s_"+algo+"_2_owd.txt"
fileName3="%s_"+algo+"_3_owd.txt"
fileName4="%s_"+algo+"_4_owd.txt"
fileOutName=algo+"_rate"
out_path=data_dir+"/"
left=0.0
right=300.0
mkdir(out_path)
instance=[1,2,3,4,5,6,7,8]
fileOut=fileOutName+".txt"
fout=open(out_path+fileOut,'w')
index=0
for case in range(len(instance)):
    f1=fileName1%(str(instance[case]))
    f4=fileName4%(str(instance[case]))
    bytes1=ReafByteInfo(f1,left,right)
    bytes4=ReafByteInfo(f4,left,right)
    f2=fileName2%(str(instance[case]))
    bytes2=ReafByteInfo(f2,left,right)
    f3=fileName3%(str(instance[case]))
    bytes3=ReafByteInfo(f3,left,right)
    sum=bytes1+bytes4
    if sum>0:
        Rate=1.0*sum*8/((right-left)*1000)
        fout.write(str(index)+"\t"+str(Rate)+"\n")
        index=index+1;
fout.close()
