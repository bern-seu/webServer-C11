CXX = g++
CFLAGS = -std=c++14 -O2 -Wall -g

TARGET = server
# 指定输出目录
BIN_DIR = ../bin

# 源文件列表
SRCS = ../code/log/*.cpp ../code/pool/*.cpp ../code/timer/*.cpp \
       ../code/http/*.cpp ../code/server/*.cpp \
       ../code/buffer/*.cpp ../code/main.cpp

# 编译目标
all:
	# 确保 bin 目录存在，不存在则创建
	mkdir -p $(BIN_DIR)
	# 编译命令
	$(CXX) $(CFLAGS) $(SRCS) -o $(BIN_DIR)/$(TARGET) -pthread -lmysqlclient

# 清理命令：只删可执行文件
clean:
	rm -rf $(BIN_DIR)/$(TARGET)