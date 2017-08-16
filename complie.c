#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <unistd.h>
#include <fcntl.h>

char *src_pos, *old_src_pos; //源码中当前正在处理的位置
char *data, *old_data;    //数据段

int *text, *old_text; //代码段
int *symbol_table; //符号表
int *current_id;        //当前正在词法分析的标识符
int token;     //当前的符号
int token_val; //符号对应的值
int *stack;    //栈
int line;      //当前行数
int *main_func;  //指向符号表中主函数的条目
int *pc, *bp, *sp, ax; //寄存器
//数据类型
enum { CHAR, INT, PTR};

//指令
enum { LEA ,IMM ,JMP ,CALL,JZ  ,JNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PUSH,
       OR ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,
       OPEN, READ, CLOS, PRTF, MALC, MSET, MCMP, EXIT };
//支持的标记类别(供词法分析器next解析成对应的标记)
enum { Num, Fun, Sys, Glo, Loc, Id, Char, Else, Enum, If, Int, Return, Sizeof,
       While, Assign, Cond, Lor, Lan, Or, Xor, And, Eq, Ne, Lt, Gt, Le, Ge, Shl,
       Shr, Add, Sub, Mul, Div, Mod, Inc, Dec, Brak};
//符号表的各条目
enum { Token, Hash, Name, Class, Type, Value, TempClass, TempType, TempValue, Size};

void next()
{
  char *begin_pos;
  int hash;
  while(token = *src_pos++)
  {
    if(token == '#')    //由于不支持宏,所以直接跳过
    {
      while(*src_pos && *src_pos != '\n' )
      {
        ++src_pos;
      }
    }
    else if(token == '\n')  //行数+1
    {
      ++line;
    }
    //解析合法的变量名(因为已经预先处理了一些标识符,如if,else等)
    else if((token >= 'a' && token <= 'z') || (token >= 'A' && token <= 'Z') || (token == '_'))
    {
      //记录标识符的起始位置
      begin_pos = src_pos - 1;
      hash = token;
      //计算标识符的hash值
      while((*src_pos >= 'a' && *src_pos <= 'z') || (*src_pos >= 'A' && *src_pos <= 'Z') || (*src_pos >= '0' && *src_pos <= '9') || *src_pos == '_')
      {
        hash = hash * 33 + *src_pos++;
      }
      //将current_id指向符号表
      current_id = symbol_table;
      while(current_id[Token])
      {
        //根据hash值还有字符来判断该标识符是否已经保存了
        if(current_id[Hash] == hash && !memcmp((char *)current_id[Name], begin_pos, src_pos - begin_pos))
        {
          //进循环了代表该标识符已经存在,则取得token
          //然后return,代表该标识符已经识别
          token = current_id[Token];
          return ;
        }
        current_id = current_id + Size;
      }
      //否则的话,保存新的标识符到符号表中
      current_id[Name] = (int)begin_pos;
      current_id[Hash] = hash;
      token = current_id[Token] = Id;
      return;
    }
    //解析数字(支持十进制,十六进制,八进制)
    else if(token >= '0' && token <= '9')
    {
      token_val = token - '0';
      //十进制
      if(token_val > 0)
      {
        while(*src_pos >= '0' && *src_pos <= '9')
        {
          token_val = token_val * 10 + *src_pos - '0';
          ++src_pos;
        }
      }
      //十六进制
      else if(*src_pos == 'x' || *src_pos == 'X')
      {
        token = *++src_pos;
        while((token >= '0' && token <= '9') || (token >= 'a' && token <= 'f') || (token >= 'A' && token <= 'F'))
        {
          token_val = token_val * 16;
          if(token >= 'a')
          {
            token_val = token_val + token - 'a' + 10;
          }
          else if(token >= 'A')
          {
            token_val = token_val + token - 'A' + 10;
          }
          else
          {
            token_val = token_val + token - '0';
          }
        }
      }
      //八进制
      else
      {
        while(*src_pos >= '0' && *src_pos <= '7')
        {
          token_val = token_val * 8 + *src_pos - '0';
          ++src_pos;
        }
      }
      //分析完数字之后,将标识设置为Num
      token = Num;
      return;
    }
    //字符串
    else if(token == '"' || token == '\'')
    {
      begin_pos = data;
      while(*src_pos && *src_pos != token)
      {
        token_val = *src_pos++;
        //解析转义符
        if(token_val == '\\')
        {
          token_val = *src_pos++;
          //只支持\n,其他的不是很大必要
          if(token_val == 'n')
          {
            token_val = '\n';
          }
        }
        //如果是字符串,则记录到data段中
        if(token == '"')
        {
          *data = token_val;
          ++data;
        }
      }
      ++src_pos;
      //如果是字符串,则将起始位置赋给token_val(单字符在循环开始就已经赋给token_val了)
      if(token == '"')
      {
        token_val = (int)begin_pos;
      }
      //将单字符的token设置为Num
      else
      {
        token = Num;
      }
      return;
    }
    //解析 //和 /
    else if(token == '/')
    {
      if(*src_pos == '/')
      {
        ++src_pos;
        while(*src_pos && *src_pos != '\n')
        {
          ++src_pos;
        }
      }
      else
      {
        token = Div;
        return ;
      }
    }
    //解析=和==
    else if(token == '=')
    {
      if(*src_pos != '=')
      {
        token = Assign;
      }
      else
      {
        ++src_pos;
        token = Eq;
      }
      return;
    }
    //解析+和++
    else if(token == '+')
    {
      if(*src_pos != '+')
      {
        token = Add;
      }
      else
      {
        ++src_pos;
        token = Inc;
      }
      return;
    }
    //解析-和--
    else if(token == '-')
    {
      if(*src_pos != '-')
      {
        token = Sub;
      }
      else
      {
        ++src_pos;
        token = Dec;
      }
      return;
    }
    //解析!=
    else if(token == '!')
    {
      if(*src_pos == '=')
      {
        ++src_pos;
        token = Ne;
      }
      return;
    }
    //解析<=和<<以及< 
    else if(token == '<')
    {
      if(*src_pos == '=')
      {
        ++src_pos;
        token = Le;
      }
      else if(*src_pos == '<')
      {
        ++src_pos;
        token = Shl;
      }
      else
      {
        token = Lt;
      }
      return;
    }
    //解析>和>=和>>
    else if(token == '>')
    {
      if(*src_pos == '=')
      {
        ++src_pos;
        token = Ge;
      }
      else if(*src_pos == '>')
      {
        ++src_pos;
        token = Shr;
      }
      else
      {
        token = Gt;
      }
      return;
    }
    //解析|和||
    else if(token == '|')
    {
      if(*src_pos != '|')
      {
        token = Or;
      }
      else
      {
        ++src_pos;
        token = Lor;
      }
      return;
    }
    //解析&和&&
    else if(token == '&')
    {
      if(*src_pos != '&')
      {
        token = And;
      }
      else
      {
        token = Lan;
      }
      return;
    }
    //解析^
    else if(token == '^')
    {
      token = Xor;
      return;
    }
    //解析%
    else if(token == '%')
    {
      token = Mod;
      return;
    }
    //解析*
    else if(token == '*')
    {
      token = Mul;
      return;
    }
    //解析[
    else if(token == '[')
    {
      token = Brak;
      return;
    }
    //解析?
    else if(token == '?')
    {
      token = Cond;
      return;
    }
    else if(token == '~' || token == ';' || token == '{' || token == '}' || token == '(' || token == ')' || token == ']' || token == ',' || token == ':')
    {
      return;
    }
  }
}
int main(int argc, char **argv)
{
  int fd, pool_size, i;
  ++argv;
  //大小
  pool_size = 256 * 1024;
  
  //给各区域申请空间
  if(!(symbol_table = malloc(pool_size)))
  {
    printf("could not malloc for symbol table\n");
    return -1;
  }
  if(!(text = old_text = malloc(pool_size)))
  {
    printf("could not malloc for text segment\n");
    return -1;
  }
  if(!(data = old_data = malloc(pool_size)))
  {
    printf("could not malloc for data segment\n");
    return -1;
  }
  if(!(stack = malloc(pool_size)))
  {
    printf("could not malloc for stack\n");
    return -1;
  }

  //init area
  memset(symbol_table, 0, pool_size);
  memset(text, 0, pool_size);
  memset(data, 0, pool_size);
  memset(stack, 0, pool_size);
  //先将这些符号存入符号表中
  src_pos = "char else enum if int return sizeof while "
        "open read close printf malloc free memset memcmp exit void main";
   
  //预先将关键字存入符号表中
  i = Char;
  while(i <= While)
  {
    next();
    current_id[Token] = i++;
  }
  //预先将系统函数存入符号表中
  i = OPEN;
  while(i <= EXIT)
  {
    next();
    current_id[Class] = Sys;
    current_id[Type] = INT;
    current_id[Value] = i++;
  }
  //处理main函数标记
  next();
  //把void处理成Char(毕竟只支持int char还有指针)
  current_id[Token] = Char;
  next();
  //指向main函数对应的符号表条目
  main_func = current_id;

  if(!(src_pos = old_src_pos = malloc(pool_size)))
  {
    printf("could not malloc for source code\n");
    return -1;
  }
  memset(src_pos, 0, pool_size);
  //打开源文件并读取到src_pos中
  if((fd = open(*argv, O_RDONLY)) < 0)
  {
    printf("could't open %s\n", *argv);
    return -1;
  }
  if((i = read(fd, src_pos, pool_size - 1)) <= 0)
  {
    printf("read soucre code error with code %d\n", i);
    return -1;
  }
  

  //释放资源
  close(fd);

  free(old_src_pos);
  free(stack);
  free(old_data);
  free(old_text);
  free(symbol_table);
  return 0;
}




