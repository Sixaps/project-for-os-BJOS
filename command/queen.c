#include "stdio.h"
#define MAX 50  
  
int board[MAX] = {0};  

int main(int argc, char * argv[])
{
	int num=atoi(argv[1]);
	if(num>0&&num<9)
	{
		Queens(num); 
	}
	else
	{
		printf("Error argument: please check your argument\n");
	}

	return 0;
}


int atoi(const char *str)
{
	int s=0;
	int flag=-1;
	
	while(*str==' ')
	{
		str++;
	}

	if(*str=='-'||*str=='+')
	{
		if(*str=='-')
		flag=0;
		str++;
	}

	while(*str>='0'&&*str<='9')
	{
		s=s*10+*str-'0';
		str++;
		if(s<0)
		{
			s=2147483647;
			break;
		}
	}
	return s*(flag?1:-1);
}


int abs(int i)
{
	if(i>0)
		return i;
	else
		return -i;
}



void Queens(int n)//求解n皇后问题的驱动函数   
{  
    int i;
      
    for (i=0; i<n/2; i++)  
    {
        board[0] = i;
        SubQueens(0, n);  
    }
        
    if (n % 2 == 1)  
    {  
        board[0] = n / 2;  
        for (i=0; i<n/2-1; i++)  
        {
            board[1] = i;
            SubQueens(1, n);  
        }
    }  
}  
  
void SubQueens(int row, int n)//求解n皇后问题的子函数   
{  
    int i, top = row;  

    board[++top] = -1;  
    while (top > row)  
    {  
        board[top]++;  
        if (top == n)//已经到最后一行，输出解   
        {   
            PrintBoard(board, n);  
            Symmetry(board, n);//输出对称解   
            top--;//返回上一行   
        }  
        else  
        {  
            if (board[top] < n && Place(top))//若本行满足要求，计算下一行位置   
                board[++top] = -1;  
            else if (board[top] == n)//已经尝试了本行所有位置，返回上一行   
                top--;  
        }  
    }  
}  
  
void Symmetry(int board[], int n)//输出对称解   
{  
    int temp[MAX] = {0};  
    int i;  
      
    for (i=0; i<n; i++)  
    {  
        temp[i] = n - 1 - board[i];  
    }  
      
    PrintBoard(temp, n);  
}  
  
int Place(int row)//判断low行皇后的位置是否可行   
{  
    int i;  
      
    for (i=0; i<row; i++)  
    {  
        if (board[i] == board[row])  
            return 0;  
        if (board[i] > board[row] && (row - i) == (board[i] - board[row]))  
            return 0;  
        if (board[i] < board[row] && (row - i) == (board[row] - board[i]))  
            return 0;  
    }  
    return 1;  
}  
  
void PrintBoard(int board[], int n)  
{  
     int i, j, flag = 1;  
      
    printf("  ");  
    printf("\n");     
      
    for (i=0; i<n; i++)  
    {    
        for (j=0; j<n; j++)  
        {  
            if (j == board[i])  
            {  
                printf("%c",2);   
            }  
            else  
            {  
                if (flag < 0)  
                    printf("%c",1);  
                else  
                    printf("%c",1);  
            }   
            flag *= -1;  
        }  
        printf("\n");  
        flag *= -1;  
    }  
        printf("\n");  
}


