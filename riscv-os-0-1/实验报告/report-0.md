由于环境配置并未有涉及到具体xv6以及我设计的代码部分，本报告仅从实验过程一方面来编写

## 实验过程部分

### 实验步骤

####    （1）安装基础依赖

sudo apt-get update
sudo apt-get install -y build-essential git python3 qemu-system-misc e
xpect gdb-multiarch

####    （2）安装RISC-V工具链

wget https://github.com/riscv-collab/riscv-gnu-toolchain/releases/down
load/2023.07.07/riscv64-elf-ubuntu-20.04-gcc-nightly-2023.07.07-nightl
y.tar.gz
sudo tar -xzf riscv64-elf-ubuntu-20.04-gcc-nightly-2023.07.07-nightly.
tar.gz -C /opt/
echo 'export PATH="/opt/riscv/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc

此处相当于使用git工具从远端库中拉取工具链包，如果出现GitHub无法打开，则需要使用科学上网

####    （3）获取xv6参考代码

git clone https://github.com/mit-pdos/xv6-riscv.git
cd xv6-riscv && make qemu # 验证能否正常运行

####    （4）创建项目结构

mkdir riscv-os && cd riscv-os
git init
mkdir -p kernel/{boot,mm,trap,proc,fs,net} include scripts

####    （5）验证环境

echo 'int main(){ return 0; }' > test.c
riscv64-unknown-elf-gcc -c test.c -o test.o
file test.o # 应显示RISC-V 64-bit
