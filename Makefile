NAME = webserv

# Convenções de C++
CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98
CPPFLAGS = -I./includes

# Cores
GREEN  = \033[0;32m
RED    = \033[0;31m
BLUE   = \033[0;34m
YELLOW = \033[0;33m
RESET  = \033[0m

# Diretórios
OBJS_DIR = objs
SRCS_DIR = src
BIN_DIR = bin

# O arquivo executável final
TARGET = $(BIN_DIR)/$(NAME)

# Busca arquivos .cpp e mapeia para .o
SRCS := $(shell find $(SRCS_DIR) -type f -name "*.cpp")
OBJS := $(patsubst $(SRCS_DIR)/%.cpp, $(OBJS_DIR)/%.o, $(SRCS))

# Regra principal
all: $(TARGET)

# Como montar o executável final
$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	@$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(OBJS) -o $@
	@printf "$(GREEN)🚀 Release binary created at $@$(RESET)\n"

# Como compilar cada .cpp em um .o
$(OBJS_DIR)/%.o: $(SRCS_DIR)/%.cpp
	@mkdir -p $(@D)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

# Limpeza dos objetos
clean:
	@rm -rf $(OBJS_DIR)
	@printf "$(RED)🧹 Object folders removed.$(RESET)\n"

# Limpeza total
fclean: clean
	@rm -rf $(BIN_DIR)
	@printf "$(RED)💥 Binaries removed.$(RESET)\n"

# Refazer tudo
re: fclean all

.PHONY: all clean fclean re
