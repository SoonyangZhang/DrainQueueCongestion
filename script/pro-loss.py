'''
https://blog.csdn.net/qq_29422251/article/details/77713741
'''
import os
import argparse
def mkdir(path):
    folder = os.path.exists(path)
    if not folder:    
        os.makedirs(path)
def ReadLossInfo(fileName):
    count=0
#cache the last line
    line=""
    for index, line in enumerate(open(fileName,'r')):
        count += 1
    lineArr = line.strip().split()
    return count,int(lineArr[1])

parser = argparse.ArgumentParser(description='manual to this script')
parser.add_argument('--algo', type=str, default ='olia')
args = parser.parse_args()
algo=args.algo
instance=[1,2,3,4,5,6,7,8]
flows=4;
data_dir="data_process"
out_path=data_dir+"/"
fileout="%s_loss_%s.txt"%(algo,str(flows))    
name="%s_%s_%s_owd.txt"
mkdir(out_path)
fout=open(out_path+fileout,'w')
for case in range(len(instance)):
    total_recv=0
    total=0
    average_loss=0.0
    exist=False
    for i in range(flows):
        filename=name%(str(instance[case]),algo,str(i+1))
        if os.path.exists(filename):
            recv,max_recv=ReadLossInfo(filename)
            total_recv+=recv
            total+=max_recv
            exist=True
    if exist:
        average_loss=float(total-total_recv)/total
        fout.write(str(instance[case])+"\t")
        fout.write(str(average_loss)+"\n")
    else:
        fout.write(str(instance[case])+"\n")
fout.close()
