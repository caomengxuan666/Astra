FROM alpine:latest

RUN sed -i 's/dl-cdn.alpinelinux.org/mirrors.ustc.edu.cn/g' /etc/apk/repositories

# 安装构建依赖
RUN apk add build-base cmake git coreutils linux-headers 

# 创建工作目录
WORKDIR /app

# 复制源代码
COPY . /app/

# 应用补丁
COPY patches/ /app/patches/
RUN patch -p1 < patches/fix-mimalloc-alpine.patch

# 构建项目
RUN mkdir build && cd build && \
    cmake .. -DBUILD_STATIC=ON -DMI_BUILD_STATIC=ON && \
    make -j$(nproc)

# 暴露端口
EXPOSE ${PORT}

# 启动命令
CMD ["/app/build/bin/Astra-CacheServer", "--bind", "0.0.0.0", "--port", "6380"]