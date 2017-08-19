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
int current_type;   //当前表达式的类型
int token;     //当前的符号
int token_val; //符号对应的值
int *stack;    //栈
int line;      //当前行数
int *main_func;  //指向符号表中主函数的条目
int *pc, *bp, *sp, ax; //寄存器
int local_offset; //局部变量在栈中的偏移
//数据类型
enum { CHAR, INT, PTR};

//指令
enum { LEA ,IMM ,JMP ,CALL,JZ  ,JNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PUSH,
       OR ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,
       OPEN, READ, CLOS, PRTF, MALC, FREE, MSET, MCMP, EXIT };
//支持的标记类别(供词法分析器next解析成对应的标记)
enum { Num = 128, Fun, Sys, Glo, Loc, Id, Char, Else, Enum, If, Int, Return, Sizeof,
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

void expression(int level)
{
  int temp, *addr;
  if(!token)
  {
    printf("%d: unexpected eof in expression\n", line);
    exit(-1);
  }
  else if(token == Num)
  {
    *++text = IMM;
    *++text = token_val;
    next();
    current_type = INT;
  }
  else if(token == '"')
  {
    *++text = IMM;
    *++text = token_val;
    next();
    while(token == '"')
    {
      next();
    }
    //在末尾追加结束符
    data = (char *)((int)data + sizeof(int) & -sizeof(int));
    current_type = PTR;
  }
  else if(token == Sizeof)
  {
    next();
    if(token == '(')
    {
      next();
    }
    else
    {
      printf("%d: expected '('\n", line);
      exit(-1);
    }
    current_type = INT;
    if(token == Int)
    {
      next();
    }
    else if(token == Char)
    {
      next();
      current_type = CHAR;
    }
    while(token == Mul)
    {
      next();
      current_type = current_type + PTR;
    }
    if(token == ')')
    {
      next();
    }
    else
    {
      printf("%d: expected ')'\n", line);
      exit(-1);
    }
    *++text = IMM;
    if(current_type == PTR || current_type == INT)
    {
      *++text = sizeof(int);
    }
    else
    {
      *++text = sizeof(char);
    }
    current_type = INT;
  }
  else if(token == Id)
  {
    addr = current_id;
    next();
    if(token == '(')
    {
      next();
      temp = 0;
      while(token != ')')
      {
        expression(Assign);
        *++text = PUSH;
        ++temp;
        if(token == ',')
        {
          next();
        }
      }
      next();
      if(addr[Class] == Sys)
      {
        *++text = addr[Value];
      }
      else if(addr[Class] == Fun)
      {
        *++text = CALL;
        *++text = addr[Value];
      }
      else
      {
        printf("%d: bad function call\n", line);
        exit(-1);
      }
      if(temp)
      {
        *++text = ADJ;
        *++text = temp;
      }
      current_type = addr[Type];
    }
    else if(addr[Class] == Num)
    {
      *++text = IMM;
      *++text = addr[Value];
      current_type = INT;
    }
    else
    {
      if(addr[Class] == Loc)
      {
        *++text = LEA;
        *++text = local_offset - addr[Value];
      }
      else if(addr[Class] == Glo)
      {
        *++text = IMM;
        *++text = addr[Value];
      }
      else
      {
        printf("%d: undefined variable\n", line);
        exit(-1);
      }
      current_type = addr[Type];
      *++text = (current_type == CHAR) ? LC : LI;
     }
  }
  else if(token == '(')
  {
    next();
    if(token == Int || token == Char)
    {
      temp = (token == Int) ? Int : Char;
      next();
      while(token == Mul)
      {
        next();
        temp = temp + PTR;
      }
      if(token == ')')
      {
        next();
      }
      else
      {
        printf("%d: bad cast\n", line);
        exit(-1);
      }
      expression(Inc);
      current_type = temp;
    }
    else
    {
      expression(Assign);
      if(token == ')')
      {
        next();
      }
      else
      {
        printf("%d: expected ')'\n", line);
        exit(-1);
      }
    }
  }
  else if(token == Mul)
  {
    next();
    expression(Inc);
    if(current_type > INT)
    {
      current_type = current_type - PTR;
    }
    else
    {
      printf("%d: bad dereference\n", line);
      exit(-1);
    }
    *++text = (current_type == CHAR) ? LC : LI;
  }
  else if(token == And)
  {
    next();
    expression(Inc);
    if(*text == LC || *text == LI)
    {
      --text;
    }
    else
    {
      printf("%d: bad address-of\n", line);
      exit(-1);
    }
    current_type = current_type + PTR;
  }
  else if(token == '!')
  {
    next();
    expression(Inc);
    *++text = PUSH;
    *++text = IMM;
    *++text = 0;
    *++text = EQ;
    current_type = INT;
  }
  else if(token == '~')
  {
    next();
    expression(Inc);
    *++text = PUSH;
    *++text = IMM;
    *++text = -1;
    *++text = XOR;
    current_type = INT;
  }
  else if(token == Add)
  {
    next();
    expression(Inc);
    current_type = INT;
  }
  else if(token == Sub)
  {
    next();
    *++text = IMM;
    if(token == Num)
    {
      *++text = -token_val;
      next();
    }
    else
    {
      *++text = -1;
      *++text = PUSH;
      expression(Inc);
      *++text = MUL;
    }
    current_type = INT;
  }
  else if(token == Inc || token == Dec)
  {
    temp = token;
    next();
    expression(Inc);
    if(*text == LC)
    {
      *text = PUSH;
      *++text = LC;
    }
    else if(*text == LI)
    {
      *text = PUSH;
      *++text = LI;
    }
    else
    {
      printf("%d: bad lvalue in pre-increment\n", line);
      exit(-1);
    }
    *++text = PUSH;
    *++text = IMM;

    *++text = (current_type > PTR) ? sizeof(int) : sizeof(char);
    *++text = (temp == Inc) ? ADD : SUB;
    *++text = (current_type == CHAR) ? SC : SI;
  }
  else
  {
    printf("%d: bad expression\n", line);
    exit(-1);
  }

  while(token >= level)
  {
    temp = current_type;
    if(token == Assign)
    {
      next();
      if(*text == LC || *text == LI)
      {
        *text = PUSH;
      }
      else
      {
        printf("%d: bad lvalue in assignment\n", line);
        exit(-1);
      }
      expression(Assign);
      current_type = temp;
      *++text = (current_type == CHAR) ? SC : SI;
    }
    else if(token == Cond)
    {
      next();
      *++text = JZ;
      addr = ++text;
      expression(Assign);
      if(token == ':')
      {
        next();
      }
      else
      {
        printf("%d: conditional missing colon\n", line);
        exit(-1);
      }
      *addr = (int)(text + 3);
      *++text = JMP;
      addr = ++text;
      expression(Cond);
      *addr = (int)(text + 1);
    }
    else if(token == Lor)
    {
      next();
      *++text = JNZ;
      addr = ++text;
      expression(Lan);
      *addr = (int)(text + 1);
      current_type = INT;
    }
    else if(token == Lan)
    {
      next();
      *++text = JZ;
      addr = ++text;
      expression(Or);
      *addr = (int)(text + 1);
      current_type = INT;
    }
    else if(token == Or)
    {
      next();
      *++text = PUSH;
      expression(Xor);
      *++text = OR;
      current_type = INT;
    }
    else if(token == Xor)
    {
      next();
      *++text = PUSH;
      expression(And);
      *++text = XOR;
      current_type = INT;
    }
    else if(token == And)
    {
      next();
      *++text = PUSH;
      expression(Eq);
      *++text = AND;
      current_type = INT;
    }
    else if(token == Eq)
    {
      next();
      *++text = PUSH;
      expression(Lt);
      *++text = EQ;
      current_type = INT;
    }
    else if(token == Ne)
    {
      next();
      *++text = PUSH;
      expression(Lt);
      *++text = NE;
      current_type = INT;
    }
    else if(token == Lt)
    {
      next();
      *++text = PUSH;
      expression(Shl);
      *++text = LT;
      current_type = INT;
    }
    else if(token == Gt)
    {
      next();
      *++text = PUSH;
      expression(Shl);
      *++text = GT;
      current_type = INT;
    }
    else if(token == Le)
    {
      next();
      *++text = PUSH;
      expression(Shl);
      *++text = LE;
      current_type = INT;
    }
    else if(token == Ge)
    {
      next();
      *++text = PUSH;
      expression(Shl);
      *++text = GE;
      current_type = INT;
    }
    else if(token == Shl)
    {
      next();
      *++text = PUSH;
      expression(Add);
      *++text = SHL;
      current_type = INT;
    }
    else if(token == Shr)
    {
      next();
      *++text = PUSH;
      expression(Add);
      *++text = SHR;
      current_type = INT;
    }
    else if(token == Add)
    {
      next();
      *++text = PUSH;
      expression(Mul);
      current_type = temp;
      if(current_type > PTR)
      {
        *++text = PUSH;
        *++text = IMM;
        *++text = sizeof(int);
        *++text = MUL;
      }
      *++text = ADD;
    }
    else if(token == Sub)
    {
      next();
      *++text = PUSH;
      expression(Mul);
      if(temp > PTR && temp == current_type)
      {
        *++text = SUB;
        *++text = PUSH;
        *++text = IMM;
        *++text = sizeof(int);
        *++text = DIV;
        current_type = INT;
      }
      else if((current_type = temp) > PTR)
      {
        *++text = PUSH;
        *++text = IMM;
        *++text = sizeof(int);
        *++text = MUL;
        *++text = SUB;
      }
      else
      {
        *++text = SUB;
      }
    }
    else if(token == Mul)
    {
      next();
      *++text = PUSH;
      expression(Inc);
      *++text = MUL;
      current_type = INT;
    }
    else if(token == Div)
    {
      next();
      *++text = PUSH;
      expression(Inc);
      *++text = DIV;
      current_type = INT;
    }
    else if(token == Mod)
    {
      next();
      *++text = PUSH;
      expression(Inc);
      *++text = MOD;
      current_type = INT;
    }
    else if(token == Inc || token == Dec)
    {
      if(*text == LC)
      {
        *text = PUSH;
        *++text = LC;
      }
      else if(*text == LI)
      {
        *text = PUSH;
        *++text = LI;
      }
      else
      {
        printf("%d: bad lvalue in post-increment\n", line);
        exit(-1);
      }
      *++text = PUSH;
      *++text = IMM;
      *++text = (current_type > PTR) ? sizeof(int) : sizeof(char);
      *++text = (token == Inc) ? ADD : SUB;
      *++text = (current_type == CHAR) ? SC : SI;
      *++text = PUSH;
      *++text = IMM;
      *++text = (current_type > PTR) ? sizeof(int) : sizeof(char);
      *++text = (token == Inc) ? SUB : ADD;
      next();
    }
    else if(token == Brak)
    {
      next();
      *++text = PUSH;
      expression(Assign);
      if(token == ']')
      {
        next();
      }
      else
      {
        printf("%d: close bracket expected\n", line);
        exit(-1);
      }
      if(temp > PTR)
      {
        *++text = PUSH;
        *++text = IMM;
        *++text = sizeof(int);
        *++text = MUL;
      }
      else if(temp < PTR)
      {
        printf("%d: pointer type expected\n", line);
        exit(-1);
      }
      *++text = ADD;
      current_type = temp - PTR;
      *++text = (current_type == CHAR) ? LC : LI;
    }
    else
    {
      printf("%d: compiler error\n", line);
      exit(-1);
    }
  }
}
//解析语句
void statement()
{
  //*a:代表条件为false时跳转到的语句
  //*b:为了防止顺序执行if-else代码,所以执行了true部分时应跳过false部分
  int *a, *b;
  //if语句
  if(token == If)
  {
    next();
    if(token == '(')
    {
      next();
    }
    else
    {
      printf("%d: expected '('\n", line);
      exit(-1);
    }
    //解析表达式
    expression(Assign);
    if(token == ')')
    {
      next();
    }
    else
    {
      printf("%d: expected ')'\n", line);
      exit(-1);
    }
    //将JZ存入text中
    *++text = JZ;
    b = ++text;

    statement();
    
    if(token == Else)
    {
      next();
      
      //将JMP B指令存入text段中
      *b = (int)(text + 3);
      *++text = JMP;
      b = ++text;


      statement();
    }
    *b = (int)(text + 1);
  }
  //while语句
  else if(token == While)
  {
    next();
    a = text + 1;
    if(token == '(')
    {
      next();
    }
    else
    {
      printf("%d: expected '('\n", line);
      exit(-1);
    }

    expression(Assign);
    
    if(token == ')')
    {
      next();
    }
    else
    {
      printf("%d: expected ')'\n", line);
      exit(-1);
    }
    *++text = JZ;
    b = ++text;
    
    statement();
    
    *++text = JMP;
    *++text = (int)a;
    *b = (int)(text + 1);
  }
  //Return语句
  else if(token == Return)
  {
    next();
    if(token != ';')
    {
      expression(Assign);
    }
    //LEV指令用来销毁函数调用栈
    *++text = LEV;
    if(token == ';')
    {
      next();
    }
    else
    {
      printf("%d: expected ';'\n", line);
      exit(-1);
    }
  }
  //其他
  else if(token == '{')
  {
    next();
    while(token != '}')
    {
      statement();
    }
    next();
  }
  else if(token == ';')
  {
    next();
  }
  else
  {
    expression(Assign);
    if(token == ';')
    {
      next();
    }
    else
    {
      printf("%d: expected ';'\n", line);
      exit(-1);
    }
  }
}



//语法分析部分
int grammar()
{
  //type:记录当前标识的类型
  //enum_value:枚举变量的值
  //para_num:参数加局部变量的个数
  int base_type, type, enum_value, para_num;

  line = 1;
  next();
  while(token)
  {
    base_type = INT;
    //int型
    if(token == Int)
    {
      next();
    }
    //char型
    else if(token == Char)
    {
      next();
      type = CHAR;
    }
    //enum型
    else if(token == Enum)
    {
      next();
      //去除多的空格
      if(token != '{')
      {
        next();
      }
      //解析enum
      if(token == '{')
      {
        next();
        enum_value = 0;
        //直到enum结束定义
        while(token != '}')
        {
          //enum赋值有两种方式,一种默认从0开始,还有一种可以自己进行赋值
          //情况1
          if(token != Id)
          {
            printf("%d: bad enum identifier\n", line);
            return -1;
          }
          next();
          //情况2
          if(token == Assign)
          {
            next();
            if(token != Num)
            {
              printf("%d: bad enum initializer\n", line);
              return -1;
            }
            enum_value = token_val;
            next();
          }
          //将当前的标识加入符号表中
          current_id[Class] = Num;
          current_id[Type] = INT;
          current_id[Value] = enum_value++;
          //还未结束,跳过逗号
          if(token == ',')
          {
            next();
          }
        }
        next();
      }
    }
    //解析函数声明或变量定义
    while(token != ';' && token != '}')
    {
      type = base_type;
      //指针变量
      while(token == Mul)
      {
        next();
        type = type + PTR;
      }
      if(token != Id)
      {
        printf("%d: bad variable declaration\n", line);
        return -1;
      }
      //判断变量/函数是否已经存在
      if(current_id[Class])
      {
        printf("%d: multiple defination\n", line);
        return -1;
      }
      next();
      current_id[Type] = type;
      //函数声明
      if(token == '(')
      {
        current_id[Class] = Fun;
        //记录该函数的地址
        current_id[Value] = (int)(text + 1);
        next();
        para_num = 0;
        //解析参数
        while(token != ')')
        {
          type = INT;
          if(token == Int)
          {
            next();
          }
          else if(token == Char)
          {
            next();
            type = CHAR;
          }
          while(token == Mul)
          {
            next();
            type = type + PTR;
          }
          if(token != Id)
          {
            printf("%d: bad parameter declaration\n", line);
            return -1;
          }
          if(current_id[Class] == Loc)
          {
            printf("%d: multiple defination\n", line);
            return -1;
          }
          //下面一系列操作都是将实参的信息保存到Tempxxx中,将形参的信息保存到xxx中
          current_id[TempClass] = current_id[Class];
          current_id[Class] = Loc;
          current_id[TempType] = current_id[Type];
          current_id[Type] = type;
          current_id[TempValue] = current_id[Value];
          current_id[Value] = para_num++;
          next();
          //如果还未结束,继续
          if(token == ',')
          {
            next();
          }
        }
        next();
        //只支持声明和定义在一起
        if(token != '{')
        {
          printf("%d: bad function defination\n", line);
          return -1;
        }
        local_offset = ++para_num;
        next();
        //接下来来到了函数体,先把变量解析了
        //遵循c89规则,即变量放在块的开头声明
        while(token == Int || token == Char)
        {
          if(token == Int)
          {
            type = Int;
          }
          else
          {
            type=  Char;
          }
          next();
          while(token != ';')
          {
            type = base_type;
            while(token == Mul)
            {
              next();
              type = type + PTR;
            }
            if(token != Id)
            {
              printf("%d: bad local declaration\n", line);
              return -1;
            }
            if(current_id[Class] == Loc)
            {
              printf("%d: multiple local variable defination\n", line);
              return -1;
            }
            //下面一系列操作都是将全局变量的信息保存到Tempxxx中,将局部变量的信息保存到xxx中
            current_id[TempClass] = current_id[Class];
            current_id[Class] = Loc;
            current_id[TempType] = current_id[Type];
            current_id[Type] = type;
            current_id[TempValue] = current_id[Value];
            current_id[Value] = ++para_num;
            next();
            if(token == ',')
            {
              next();
            }
          }
          next();
        }
        //为局部变量的入栈申请空间
        *++text = ENT;  
        *++text = para_num - local_offset;
        while(token != '}')
        {
          statement();
        }
        //函数解析完毕,弹栈
        *++text = LEV;
        //恢复全局变量的信息
        current_id = symbol_table;
        while(current_id[Token])
        {
          if(current_id[Class] == Loc)
          {
            current_id[Class] = current_id[TempClass];
            current_id[Type] = current_id[TempType];
            current_id[Value] = current_id[TempValue];
          }
          current_id = current_id + Size;
        }
      }
      //全局变量
      else
      {
        current_id[Class] = Glo;
        current_id[Value] = (int)data;
        data = data + sizeof(int);
      }
      if(token == ',')
      {
        next();
      }
    }
    next();
  }
}

int run()
{
  int i, *temp;
  while(1)
  {
    i = *pc++;
    if(i == LEA)
    {
      ax = (int)(bp + *pc++);
    }
    else if(i == IMM)
    {
      ax = *pc++;
    }
    else if(i == JMP)
    {
      pc = (int *)*pc;
    }
    else if(i == CALL)
    {
      *--sp = (int)(pc + 1);
      pc = (int *)*pc;
    }
    else if(i == JZ)
    {
      pc = ax ? pc + 1 : (int *)*pc;
    }
    else if(i == JNZ)
    {
      pc = ax ? (int *)*pc : pc + 1;
    }
    else if(i == ENT)
    {
      *--sp = (int)bp;
      bp = sp;
      sp = sp - *pc++;
    }
    else if(i == ADJ)
    {
      sp = sp + *pc++;
    }
    else if(i == LEV)
    {
      sp = bp;
      bp = (int *)*sp++;
      pc = (int *)*sp++;
    }
    else if(i == LI)
    {
      ax = *(int *)ax;
    }
    else if(i == LC)
    {
      ax = *(char *)ax;
    }
    else if(i == SI)
    {
      *(int *)*sp++ = ax;
    }
    else if(i == SC)
    {
      ax = *(char *)*sp++ = ax;
    }
    else if(i == PUSH)
    {
      *--sp = ax;
    }
    else if(i == OR)
    {
      ax = *sp++ | ax;
    }
    else if(i == XOR)
    {
      ax = *sp++ ^ ax;
    }
    else if(i == AND)
    {
      ax = *sp++ & ax;
    }
    else if(i == EQ)
    {
      ax = *sp++ == ax;
    }
    else if(i == NE)
    {
      ax = *sp++ != ax;
    }
    else if(i == LT)
    {
      ax = *sp++ < ax;
    }
    else if(i == GT)
    {
      ax = *sp++ > ax;
    }
    else if(i == LE)
    {
      ax = *sp++ <= ax;
    }
    else if(i == GE)
    {
      ax = *sp++ >= ax;
    }
    else if(i == SHL)
    {
      ax = *sp++ << ax;
    }
    else if(i == SHR)
    {
      ax = *sp++ >> ax;
    }
    else if(i == ADD)
    {
      ax = *sp++ + ax;
    }
    else if(i == SUB)
    {
      ax = *sp++ - ax;
    }
    else if(i == MUL)
    {
      ax = *sp++ * ax;
    }
    else if(i == DIV)
    {
      ax = *sp++ / ax;
    }
    else if(i == MOD)
    {
      ax = *sp++ % ax;
    }
    else if(i == OPEN)
    {
      ax = open((char *)sp[1], *sp);
    }
    else if(i == READ)
    {
      ax = read(sp[2], (char *)sp[1], *sp);
    }
    else if(i == CLOS)
    {
      ax = close(*sp);
    }
    else if(i == PRTF)
    {
      temp = sp + pc[1];
      ax = printf((char *)temp[-1], temp[-2], temp[-3], temp[-4], temp[-5], temp[-6]);
    }
    else if(i == MALC)
    {
      ax = (int)malloc(*sp);
    }
    else if(i == FREE)
    {
      free((void *)*sp);
    }
    else if(i == MSET)
    {
      ax = (int)memset((char *)sp[2], sp[1], *sp);
    }
    else if(i == MCMP)
    {
      ax = memcmp((char *)sp[2], (char *)sp[1], *sp);
    }
    else if(i == EXIT)
    {
      printf("exit(%d)\n", *sp);
      return *sp;
    }
    else
    {
      printf("unknown instruction = %d\n", i);
      return -1;
    }
  }

}

int main(int argc, char **argv)
{
  int fd, pool_size, i;
  int *temp;
  int res;
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

  grammar();
  if(!(pc = (int *)main_func[Value]))
  {
    printf("main() not defined\n");
    return -1;
  }

  sp = (int *)((int)stack + pool_size);
  *--sp = EXIT;
  *--sp = PUSH;
  temp = sp;
  *--sp = argc;
  *--sp = (int)argv;
  *--sp = (int)temp;

  res = run();
  free(old_src_pos);
  free(stack);
  free(old_data);
  free(old_text);
  free(symbol_table);
  return res;
}




