'''
reference
https://blog.csdn.net/zhang_gang1989/article/details/72884662
https://blog.csdn.net/weixin_38215395/article/details/78679296
'''
#change lineArr[1] the index accoring to the data trace format
import os
import argparse
def mkdir(path):
    folder = os.path.exists(path)
    if not folder:    
        os.makedirs(path)
def ReadDelayData(fileName):
    sum_owd=0.0;
    sum_lines=0;    
    with open(fileName) as txtData:
        for line in txtData.readlines():
            lineArr = line.strip().split()
            sum_owd+=float(lineArr[2])
            sum_lines+=1;
    return  sum_owd,sum_lines
    
parser = argparse.ArgumentParser(description='manual to this script')
parser.add_argument('--algo', type=str, default ='olia')
args = parser.parse_args()
algo=args.algo
instance=[1,2,3,4,5,6,7,8]
flows=4;
data_dir="data_process"
out_path=data_dir+"/"
fileout="%s_owd.txt"%(algo)    
name="%s_%s_%s_owd.txt"
mkdir(out_path)
fout=open(out_path+fileout,'w')
for case in range(len(instance)):
    total_owd=0.0
    total_lines=0
    average_owd=0.0
    exist=False
    for i in range(flows):
        sum_delay=0.0
        sum_lines=0
        average_owd=0.0
        filename=name%(str(instance[case]),algo,str(i+1))
        if os.path.exists(filename):
            sum_delay,sum_lines=ReadDelayData(filename)
            total_owd+=sum_delay
            total_lines+=sum_lines
            exist=True
    if exist:
        average_owd=total_owd/total_lines
        fout.write(str(instance[case])+"\t")
        fout.write(str(average_owd)+"\n")
    else:
        fout.write(str(instance[case])+"\n")
fout.close()        
