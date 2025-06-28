# Nome do executável
TARGET = main

# Compilador
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

# Arquivos-fonte
SRCS = main.cpp SlowPack.cpp UUIDPack.cpp ConnectCentral.cpp

# Arquivos-objeto (transforma .cpp em .o)
OBJS = $(SRCS:.cpp=.o)

# Regra padrão
all: $(TARGET)

# Como compilar o executável final
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Regra para remover arquivos gerados
clean:
	rm -f $(TARGET) $(OBJS)

# Recompila tudo do zero
rebuild: clean all

run: $(TARGET)
	./$(TARGET)
