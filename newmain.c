/*
 * File:   newmain.c
 * Author: kojim
 *
 * Created on 2024/05/16, 22:28
 */


/*
 * メモ
 * 方向切り替え中の動作について
 * 現状態ではオーバードライブを行っていない物とする
 * 方向切り替え間に受信したタイミングでオーバードライブカウントフラグを代入する
 * その後，オンフラグを立てる
 * 方向切り替えが完了したタイミングでフラグの2ビット目がたって動作を開始する
 * 
 * 次に，現状態で働いている場合
 * 受信したタイミングでオーバードライブカウントフラグを代入する．
 * その後オンフラグを立てる
 * 方向切り替え中は方向切り替えフラグを監視して完了するまで2ビット目を立てないことで動作を行わないようにする
 * 上の状態ではオンフラグを消さない
 * 方向切り替えが完了してフラグが消えた場合，かつ，台形制御の増加が開始した場合にオーバードライブを開始する．
 * 
 * オーバードライブ動作中にデューティー比減少の命令が来た場合
 * まず，逆転の場合にもデューティー比が減少する
 * この場合でも動作するような物にしなければならない
 * 
 * それで，オンビットを取り消すのは停止命令（デューティー比0が入力されたとき，ブレーキ命令が入力されたとき，モーターオフ命令が入力されたとき）に行う物とする
 * 自動で取り消されるタイミングはオーバードライブが完了したタイミングとなる．;
 * 
 * 上の物に従ってプログラムを作成する．
 * 
 */

// PIC16F18313 Configuration Bit Settings

// 'C' source line config statements

// CONFIG1
#pragma config FEXTOSC = OFF    // FEXTOSC External Oscillator mode Selection bits (Oscillator not enabled)
#pragma config RSTOSC = HFINT32 // Power-up default value for COSC bits (HFINTOSC with 2x PLL (32MHz))
#pragma config CLKOUTEN = OFF   // Clock Out Enable bit (CLKOUT function is disabled; I/O or oscillator function on OSC2)
#pragma config CSWEN = OFF      // Clock Switch Enable bit (The NOSC and NDIV bits cannot be changed by user software)
#pragma config FCMEN = OFF      // Fail-Safe Clock Monitor Enable (Fail-Safe Clock Monitor is disabled)

// CONFIG2
#pragma config MCLRE = OFF      // Master Clear Enable bit (MCLR/VPP pin function is digital input; MCLR internally disabled; Weak pull-up under control of port pin's WPU control bit.)
#pragma config PWRTE = ON       // Power-up Timer Enable bit (PWRT enabled)
#pragma config WDTE = OFF       // Watchdog Timer Enable bits (WDT disabled; SWDTEN is ignored)
#pragma config LPBOREN = OFF    // Low-power BOR enable bit (ULPBOR disabled)
#pragma config BOREN = OFF      // Brown-out Reset Enable bits (Brown-out Reset disabled)
#pragma config BORV = LOW       // Brown-out Reset Voltage selection bit (Brown-out voltage (Vbor) set to 2.45V)
#pragma config PPS1WAY = ON     // PPSLOCK bit One-Way Set Enable bit (The PPSLOCK bit can be cleared and set only once; PPS registers remain locked after one clear/set cycle)
#pragma config STVREN = ON      // Stack Overflow/Underflow Reset Enable bit (Stack Overflow or Underflow will cause a Reset)
#pragma config DEBUG = OFF      // Debugger enable bit (Background debugger disabled)

// CONFIG3
#pragma config WRT = OFF        // User NVM self-write protection bits (Write protection off)
#pragma config LVP = OFF        // Low Voltage Programming Enable bit (HV on MCLR/VPP must be used for programming.)
//RA3を入力として使用するために、これをオフにする必要がある。

// CONFIG4
#pragma config CP = OFF         // User NVM Program Memory Code Protection bit (User NVM code protection disabled)
#pragma config CPD = OFF        // Data NVM Memory Code Protection bit (Data NVM code protection disabled)

// #pragma config statements should precede project file includes.
// Use project enums instead of #define for ON and OFF.


#include <xc.h>
#define _XTAL_FREQ 32000000
/*
 * ピンアサイン
 * 0:PWM_ch1,1/PWM3
 * 1:PWm_ch1,2/PWM4
 * 2:I2C_SDA
 * 3:I2C_SCL
 * 4:PWM_ch2,1/PWM5
 * 5;PWM_cH2,2/PWM6
 */

/*
 * コメント
 * このプログラムは元々16F886用に作成したものを流用しています．
 * すべての部分に手を入れてあるので動きますが，せつめいが所々変です．
 * 直す元気がなかったからです．
 * これは16F15313用に書いてますが，16F18313用に書き換えたかったらPWM3とPWM4の部分をCCP1,CCP2に変更
 * デューティー比設定用にCCPRxL,Hを左詰にしてPPSを書き換えたら動くと思います．(もちろんほかの設定用のレジスタはいじるけど)
 */

//I2C---------------------------------------------------------------------------

#define I2C_ADD 0x12//9

char I2C_BUFF = 0;
char I2C_SEND = 0;
char I2C_COUNT = 0;
char I2C_ORDER = 0;

//MAIN
/*
 * 
 * 命令の仕様として命令とデータを使用して通信を行う
 * 別のマイコンからこのマイコンに対してデータが送られてくる場合
 * 命令バイトの最上位ビットは0とする
 * ２バイト：まず、命令バイトを入力した後に、それに対応するデータが来る
 * １バイト：命令だけで動作が決まるものはこれで動作を行う
 * 
 * 別のマイコンに対してデータを送信する場合(使いそうなら送信部分のプログラムも書く)
 * 命令バイトの最上位ビットは1とする
 * まず命令を受信してからマスターはアドレスを送信するときに受信にする
 * その後、このマイコンが先ほどの命令に対応したデータを送信する
 * 
 * 命令の連続受信はマスターがRSENを送り連続的に処理する方法をとる（仮）
 * 
 */


//以下命令のマクロ
//左側のモーターに対する命令
//#define MOTOR_L_DUTY 0x10//デューティー比を変更させるための命令
//#define MOTOR_L_FOR  0x11//正転させるための命令
//#define MOTOR_L_REV  0x12//逆転させるための命令
//
////右側のモーターに対する命令
//#define MOTOR_R_DUTY 0x13//デューティー比を変更させるための命令
//#define MOTOR_R_FOR  0x14//正転させるための命令
//#define MOTOR_R_REV  0x15//逆転させるための命令

//テスト用の命令
//左側のモーターに対する命令
#define MOTOR_L_DUTY 'A'//デューティー比を変更させるための命令
#define MOTOR_L_FOR  'B'//正転させるための命令
#define MOTOR_L_REV  'C'//逆転させるための命令

//右側のモーターに対する命令
#define MOTOR_R_DUTY 'D'//デューティー比を変更させるための命令
#define MOTOR_R_FOR  'E'//正転させるための命令
#define MOTOR_R_REV  'F'//逆転させるための命令

//ブレーキ開始の動作
#define MOTOR_L_STOP 'G'//左側のブレーキ
#define MOTOR_R_STOP 'H'//右側

//すべての出力を0にする命令
#define MOTOR_L_OFF  'I'
#define MOTOR_R_OFF  'J'

#define CHANGE_ODD 'X'//オーバードライブのデューティー比を変更するこまんど


//MOTOR-------------------------------------------------------------------------
/*
 * もちろん台形制御もしたいし、デッドタイムも入れる必要がある
 * タイマー2はPWMで使用されているからそれをうまく使用してデッドタイムと台形制御を入れる
 * 
 * モーターの制御の方法として、デューティー比0にセットされて台形制御が完了した場合に自動的にモーターの回転方向をリセットする
 * 台形制御でモーターを停止しているときに逆方向の信号が入ってきた場合にはそれに対応するフラグをセットする。
 * 逆方向の信号を受信した後にデューティー比を変更する命令が入ってきたときはそれを一時的に保存して一度モーターが停止したタイミングで受信したデューティー比に台形制御する
 * とりあえずそれで書いていく
 * 
 * PWMは基本8BITで使用する（そのため255をセットしても若干出力がLoの時間がある)
 * ブレーキを使用するときは10bitの値を全部1でうめてLoが出ないようにする．
 * ブレーキを解除するときに最下位ビットの1をクリアするのを忘れないこと
 * 
 */


//台形制御用変数
short DUTY_L_NOW = 0;//台形制御時に減算すると0より小さくなりいきなりMAXになるのを防止するため
//現在のPWMのデューティー比はCCPのレジスタを参照のこと
char DUTY_L_TARGET = 0;//台形制御用の目標値を格納
char DUTY_L_KEEP = 0;//逆回転を行うときにデューティー比を格納するために使用

short DUTY_R_NOW = 0;
char DUTY_R_TARGET = 0;
char DUTY_R_KEEP = 0;


//台形制御用パラメータ
#define DUTY_UP 5//一回の台形制御で上がるデューティー比
#define DUTY_DOWN 5//下がる
//#define DUTY_COUNT_UP 0x7C


//#define DUTY_COUNT_DOWN 0x7C
//↑実際に使用する台形制御用遅延時間

#define DUTY_COUNT_UP 0xFF
#define DUTY_COUNT_DOWN 0xFF
//TEST

//#define DUTY_COUNT_UP 0xFF
//#define DUTY_COUNT_DOWN 0xFF
//↑カウントを行わないですぐに台形制御を行う場合
//#define DUTY_COUNT_UP 0x7E
//#define DUTY_COUNT_DOWN 0x7E
//↑カウントを行ってから台形制御を行う場合(カウント2回する)
//上のマクロはタイマー割込み何回に一回台形制御を行うかのマクロ
//7:台形制御を行うかカウントを行うか(1:台形制御を行う 0:カウントを行う)
//0の場合カウントが行われるが、カウントが終了すると最上位ビットが1になる。この時台形制御が行われる

char DUTY_COUNT_L = 0;
char DUTY_COUNT_R = 0;
//この変数は台形制御を行う間隔をカウントするための変数


//PWM管理用変数
char PWM_FLAG1 = 0;//色々なフラグを格納する
//LCH
//3:L,増加 2:L,減少 1:L,0フラグ 0:L,方向切り替え中
//RCHは7~4で↑のLをRと読み替えて書く

char PWM_FLAG2 = 0;
//3:LCHのPWMのデューティー比が0になり、逆転するためのフラグが立っている場合に逆転させる方向を決める(1:逆転 0:正転)
//2:ブレーキ動作を行う。これが1のときにデューティー比が0になるとブレーキがかかるようになる。ちなみに、これが1だと上のビットは無視される。
//RCHは7~4で↑のLをRと読み替えて書く

char PWM_OUTPUT_FLAG = 0;
//2:L,ブレーキ 1:L,逆転 0:L,正転
//RCHは7~4で↑のLをRと読み替えて書く

//PWMの出力ピンを把握するためにつかう変数を扱いやすくするためにするデファイン
#define MOTOR_1_A 0x01
#define NOT_MOTOR_1_A 0xFE
#define MOTOR_1_B 0x02
#define NOT_MOTOR_1_B 0xFD
//ブレーキ動作
#define MOTOR_1_BRAKE 0x04
#define NOT_MOTOR_1_BRAKE 0xFB
//ストップ動作
#define NOT_MOTOR_1_STOP 0xFC

#define MOTOR_2_A 0x10
#define NOT_MOTOR_2_A 0xEF
#define MOTOR_2_B 0x20
#define NOT_MOTOR_2_B 0xDF
//ブレーキ動作
#define MOTOR_2_BRAKE 0x40
#define NOT_MOTOR_2_BRAKE 0xBF
//ストップ動作
#define NOT_MOTOR_2_STOP 0xCF

//デッドタイム
/*
 * デッドタイムは以下のように動作させる
 * まず、カウンタ用の変数がある。
 * タイマーを動作させる場合は最上位ビットを1にする必要がある
 * 1の場合はカウンタが増加するようになる
 * オーバーフローするとすべて0になる。
 * この方法をとると最大値が128になる
 * さらにもう一つの変数を使用して、その後の動作を決定する。
 */

//現在値
#define DEAD_TIME 0xFF//0b1111 1110
//20MHz 1:16 専用

//2回でデッドタイム終了(内部発振専用)
//データシートに書かれているデッドタイムの時間を満たすことができるようにする(0.1ms)

char DEAD_TIME_L = 0;
//7:デッドタイムカウンタ動作ビット(1:動作 0:待機) 6~0カウンタ
char DEAD_TIME_R = 0;
char DEAD_TIME_FLAG = 0;
//デッドタイムのフラグ
//3:Lのデッドタイムがオンの状態 2:PWMストップフラグ 1:デッドタイムのカウントが終了したときにブレーキ状態に移行する 0:ポート操作ビット（1:デッドタイムが終了した場合にポートを逆転(B)の状態にする 0:デッドタイムが終了した場合にポートを正転(A)の状態にする
//7~4:↑の物をRに置き換えたもの

/*
 * PWMストップフラグに対しての説明
 * これはデューティー比が0のときに1にするとPWMの計算がスキップされて動作しなくなるもの。なので、デューティー比が0じゃない場合は手動で様々な変数をリセットし、CCPRxLレジスタに0を入れる必要がある。
 * そうすると、デューティー比が自由に変更できるようになる
 * 
 *  
 * PWMのデューティー比を0にするとモーターの出力が0になることについての補足
 * これは↑の動作で問題ない。現在のデューティー比が0のときでも動作する。←これも問題ない
 * 何が問題化というと、台形制御に使うカウントの時間はポートが変化しないため、この時間が空くことが問題である。
 * そのため、モーターの出力をなしにする命令を追加する。
 * 
 * ブレーキ動作についての補足説明
 * オシロで見たらデューティー比100%の時でも若干LOの時間がある
 * 気に食わないのでPPSをいじってLATで動作させることにする
 * 面倒なのでPPS1WAYをオフにする
 */


/*
 * スタート時モーターが始動しない可能性があるため始動時に一瞬だけデューティー比を大きく（オーバードライブ）して始動させる方法をとる．
 * まぁモーターが調子悪いのでどうだろうね
 */

char PWM_OVD_DUTY = 128;//ボーバードライブのデューティー比
#define PWM_OVD_TIME 200  //カウンタ代入用
//上のマクロはタイマー割込み何回でスタート時のオーバードライブを停止させるかのマクロ
//動作させたい回数をそのまま指定する．0になったら自動で停止する
//0の場合カウントが行われオーバードライブが働くが、カウントが終了すると最上位ビットが1になり停止する。

char PWM_L_OVD_COUNT = 0;//カウンタ用変数
char PWM_R_OVD_COUNT = 0;

char PWM3DCH_BUFF = 0;//PWMデューティー比格納用変数．コレを使用することでオーバードライブ時のデータ入力時等の誤動作を防ぐ
char PWM4DCH_BUFF = 0;
char PWM5DCH_BUFF = 0;
char PWM6DCH_BUFF = 0;

char PWM_OVD_FLAG = 0;
//PWMオーバードライブ用フラグ格納変数
//CH_L
//1:確実に始動時のみ1になる変数0:オーバードライブを実行するか．このフラグは完了すると自動的にクリアされる．
//CH_R
//4:うえとおなじ

#define OVD_EN_1 0x01
#define OVD_ON_1 0x02

#define OVD_EN_2 0x10
#define OVD_ON_2 0x20


//スロースタートモード

#define OVD_EN_1 0x01
#define OVD_ON_1 0x02

#define OVD_EN_2 0x10
#define OVD_ON_2 0x20


//移植用(PIC 16F15313 > PIC 16F18313)
//レジスタを置き換えることすら面倒というか，付け足せるようにマクロしておく
#define PWM3DCH CCPR1H
#define PWM3DCL CCPR1L
#define PWM4DCH CCPR2H
#define PWM4DCL CCPR2L

#define CH1_1_PPS 0b01100//CCP1
#define CH1_2_PPS 0b01101//CCP2
#define CH2_1_PPS 0b00010//PWM5
#define CH2_2_PPS 0b00011//PWM6
#define PPS_LATXY 0

void main(void) {
    //PORT SETTING--------------------------------------------------------------
    TRISA = 0x0C;
    ANSELA = 0x00;
    LATA = 0x33;
    
    //PPS SETTING---------------------------------------------------------------
    //I2C
    RA2PPS = 0b11001;//SDA
    SSP1DATPPS = 2;//RA2
    SSP1CLKPPS = 3;//RA3
    
    //PWM
    RA0PPS = CH1_1_PPS;
    RA1PPS = CH1_2_PPS;
    RA4PPS = CH2_1_PPS;
    RA5PPS = CH2_2_PPS;
    
    //INT SETTING---------------------------------------------------------------
    INTCON = 0xC0;
    PIE1 = 0x0A;//SSP1IE TMR2IE
    
    //MSSP SETTING--------------------------------------------------------------
    //I2C_7BITADD
    SSP1STAT = 0b10000000;
    SSP1CON1 = 0b00110110;
    SSP1CON2 = 0x00;
    SSP1CON3 = 0x00;
    SSP1MSK = 0xFE;
    SSP1ADD = I2C_ADD;
    
    //TIMER2 SETTING------------------------------------------------------------
    //ここの部分はテストしてもらう
    T2CON = 0b00011110;//POST1:1 PRE1:16
    PR2 = 0xFF;
    
    //PWM SETTING---------------------------------------------------------------
    //PWM5
    PWM5CON = 0x80;
    PWM5DCH = 0x00;
    PWM5DCL = 0x00;
    
    //PWM6
    PWM6CON = 0x80;
    PWM6DCH = 0x00;
    PWM6DCL = 0x00;
    
    //CCP SETTING---------------------------------------------------------------
    //CCP1
    CCP1CON = 0x9F;
    CCPR1H = 0x00;
    CCPR1L = 0x00;
    
    //CCP2
    CCP2CON = 0x9F;
    CCPR2H = 0x00;
    CCPR2L = 0x00;
    
    
    while(1);
    return;
}

void __interrupt() isr(void){
    if(SSP1IF){//MSSP INT
        //動作の説明
        /*
         * まず、一番初めにアドレスを受信したときは受信・送信バイト数のカウントの変数をインクリメントする
         * はじめのバイトのデータは命令なのでそれをどこかの件数に保存する必要がある
         * このマイコンが書き込みの動作を行う場合は、前の命令を保持する必要があるため命令だけ削除しないようなプログラムとする
         */
        I2C_BUFF = SSP1BUF;
        if(D_nA == 0){//アドレス受信のとき
            I2C_COUNT = 0;//カウントリセット
            if(R_nW == 0){
                I2C_ORDER = 0;//命令リセット
            }
        }
        if(R_nW){//送信のとき
            
        }
        else if(D_nA){//データ受信のとき
            if(I2C_COUNT == 0){//命令受信のとき
                I2C_ORDER = I2C_BUFF;
                //ここから命令実行
                if(I2C_ORDER == MOTOR_L_FOR){//L 正転
                    //ブレーキ動作の場合の判定を書かなければいけない
                    if(PWM_OUTPUT_FLAG & MOTOR_1_BRAKE){//現在ブレーキ動作の場合
                        RA0PPS = CH1_1_PPS;
                        RA1PPS = CH1_2_PPS;
                        PWM_OUTPUT_FLAG &= NOT_MOTOR_1_BRAKE;
                        DEAD_TIME_FLAG &= 0xF8;
                        DEAD_TIME_FLAG |= 0x08;
                        DEAD_TIME_L = DEAD_TIME;
                        PWM_L_OVD_COUNT = PWM_OVD_TIME;
                        PWM_OVD_FLAG |= OVD_EN_1;
                    }
                    else if(PWM_OUTPUT_FLAG & MOTOR_1_B){//現在逆転動作の場合
                        PWM_FLAG1 |= 0x07;
                        PWM_FLAG1 &= 0xF7;
                        PWM_FLAG2 &= 0xF0;
                        DUTY_COUNT_L = DUTY_COUNT_DOWN;
                        DUTY_L_TARGET = 0;
                        PWM_L_OVD_COUNT = PWM_OVD_TIME;
                        PWM_OVD_FLAG |= OVD_EN_1;
                    }
                    else if(PWM_OUTPUT_FLAG & MOTOR_1_A){//現在正転している場合 (逆転キャンセル)
                        PWM_FLAG1 &= 0xF0;
                        PWM_OVD_FLAG |= OVD_EN_1;
                        
                    }
                    else{//残りの場合　この場合はどっちにも回転していない場合　デッドタイムを入れて回転させる
                        if(DEAD_TIME_L & 0x80){//デッドタイムのカウンタが作動中の場合にデッドタイムの動作のフラグを立てる
                            DEAD_TIME_FLAG |= 0x09;
                            DEAD_TIME_FLAG &= 0xF9;
                            PWM_L_OVD_COUNT = PWM_OVD_TIME;
                            PWM_OVD_FLAG |= OVD_EN_1;
                        }
                        else{
                            PWM_OUTPUT_FLAG |= MOTOR_1_A;
                            DEAD_TIME_L = DEAD_TIME;
                            PWM_L_OVD_COUNT = PWM_OVD_TIME;
                            PWM_OVD_FLAG |= OVD_EN_1;
                        }
                    }
                }
                else if(I2C_ORDER == MOTOR_L_REV){//{L 逆転
                    if(PWM_OUTPUT_FLAG & MOTOR_1_BRAKE){
                        RA0PPS = CH1_1_PPS;
                        RA1PPS = CH1_2_PPS;
                        PWM_OUTPUT_FLAG &= NOT_MOTOR_1_BRAKE;
                        DEAD_TIME_FLAG &= 0xF9;
                        DEAD_TIME_FLAG |= 0x09;
                        DEAD_TIME_L = DEAD_TIME;
                        PWM_L_OVD_COUNT = PWM_OVD_TIME;
                        PWM_OVD_FLAG |= OVD_EN_1;
                    }
                    else if(PWM_OUTPUT_FLAG & MOTOR_1_A){
                        PWM_FLAG1 |= 0x07;
                        PWM_FLAG1 &= 0xF7;
                        PWM_FLAG2 |= 0x08;
                        PWM_FLAG2 &= 0xF8;
                        DUTY_COUNT_L = DUTY_COUNT_DOWN;
                        DUTY_L_TARGET = 0;
                        PWM_L_OVD_COUNT = PWM_OVD_TIME;
                        PWM_OVD_FLAG |= OVD_EN_1;
                        
                    }
                    else if(PWM_OUTPUT_FLAG & MOTOR_1_B){
                        PWM_FLAG1 &= 0xF0;
                        PWM_OVD_FLAG |= OVD_EN_1;
                    }
                    else{
                        if(DEAD_TIME_L & 0x80){
                            DEAD_TIME_FLAG |= 0x0A;
                            DEAD_TIME_FLAG &= 0xFA;
                            PWM_L_OVD_COUNT = PWM_OVD_TIME;
                            PWM_OVD_FLAG |= OVD_EN_1;
                        }
                        else{
                            PWM_OUTPUT_FLAG |= MOTOR_1_B;
                            DEAD_TIME_L = DEAD_TIME;
                            PWM_L_OVD_COUNT = PWM_OVD_TIME;
                            PWM_OVD_FLAG |= OVD_EN_1;
                        }
                    }
                }
                else if(I2C_ORDER == MOTOR_R_FOR){//R 正転
                    if(PWM_OUTPUT_FLAG & MOTOR_2_BRAKE){
                        RA4PPS = CH2_1_PPS;
                        RA5PPS = CH2_2_PPS;
                        PWM_OUTPUT_FLAG &= NOT_MOTOR_2_BRAKE;
                        DEAD_TIME_FLAG &= 0x8F;
                        DEAD_TIME_FLAG |= 0x80;
                        DEAD_TIME_R = DEAD_TIME;
                        PWM_R_OVD_COUNT = PWM_OVD_TIME;
                        PWM_OVD_FLAG |= OVD_ON_2;
                        
                    }
                    else if(PWM_OUTPUT_FLAG & MOTOR_2_B){
                        PWM_FLAG1 |= 0x70;
                        PWM_FLAG1 &= 0x7F;
                        PWM_FLAG2 &= 0x0F;
                        DUTY_COUNT_R = DUTY_COUNT_DOWN;
                        DUTY_R_TARGET = 0;
                        PWM_R_OVD_COUNT = PWM_OVD_TIME;
                        PWM_OVD_FLAG |= OVD_ON_2;
                    }
                    else if(PWM_OUTPUT_FLAG & MOTOR_2_A){
                        PWM_FLAG1 &= 0x0F;
                        PWM_OVD_FLAG |= OVD_ON_2;
                    }
                    else{
                        if(DEAD_TIME_R & 0x80){
                            DEAD_TIME_FLAG |= 0x90;
                            DEAD_TIME_FLAG &= 0x9F;
                            PWM_R_OVD_COUNT = PWM_OVD_TIME;
                            PWM_OVD_FLAG |= OVD_ON_2;
                        }
                        else{
                            PWM_OUTPUT_FLAG |= MOTOR_2_A;
                            DEAD_TIME_R = DEAD_TIME;
                            PWM_R_OVD_COUNT = PWM_OVD_TIME;
                            PWM_OVD_FLAG |= OVD_ON_2;
                        }
                    }
                }
                else if(I2C_ORDER == MOTOR_R_REV){//R 逆転
                    if(PWM_OUTPUT_FLAG & MOTOR_2_BRAKE){
                        RA4PPS = CH2_1_PPS;
                        RA5PPS = CH2_2_PPS;
                        PWM_OUTPUT_FLAG &= NOT_MOTOR_2_BRAKE;
                        DEAD_TIME_FLAG &= 0x9F;
                        DEAD_TIME_FLAG |= 0x90;
                        DEAD_TIME_R = DEAD_TIME;
                        PWM_R_OVD_COUNT = PWM_OVD_TIME;
                        PWM_OVD_FLAG |= OVD_ON_2;
                    }
                    else if(PWM_OUTPUT_FLAG & MOTOR_2_A){
                        PWM_FLAG1 |= 0x70;
                        PWM_FLAG1 &= 0x7F;
                        PWM_FLAG2 |= 0x80;
                        PWM_FLAG2 &= 0x8F;
                        DUTY_COUNT_R = DUTY_COUNT_DOWN;
                        DUTY_R_TARGET = 0;
                        PWM_R_OVD_COUNT = PWM_OVD_TIME;
                        PWM_OVD_FLAG |= OVD_ON_2;
                    }
                    else if(PWM_OUTPUT_FLAG & MOTOR_2_B){
                        PWM_FLAG1 &= 0x0F;
                        PWM_OVD_FLAG |= OVD_ON_2;
                    }
                    else{//現在逆転している、もしくは回転していない場合
                        if(DEAD_TIME_R & 0x80){
                            DEAD_TIME_FLAG |= 0xA0;
                            DEAD_TIME_FLAG &= 0xAF;
                            PWM_R_OVD_COUNT = PWM_OVD_TIME;
                            PWM_OVD_FLAG |= OVD_ON_2;
                        }
                        else{
                            PWM_OUTPUT_FLAG |= MOTOR_2_B;
                            DEAD_TIME_R = DEAD_TIME;
                            PWM_R_OVD_COUNT = PWM_OVD_TIME;
                            PWM_OVD_FLAG |= OVD_ON_2;
                        }
                    }
                }
                else if(I2C_ORDER == MOTOR_L_STOP){//L ブレーキ
                    if(PWM_OUTPUT_FLAG & MOTOR_1_BRAKE);//現在ブレーキ動作している場合は何もしないので省略
                    //だけど，ブレーキ動作中にブレーキ命令が入って余計なことをしないように判定は入れてすべてスキップさせる
                    else if(PWM_OUTPUT_FLAG & 0x03){//どっちかに回転している場合
                        PWM_FLAG1 |= 0x07;
                        PWM_FLAG1 &= 0xF7;
                        PWM_FLAG2 &= 0xFB;
                        PWM_FLAG2 |= 0x04;
                        DUTY_COUNT_L = DUTY_COUNT_DOWN;
                        DUTY_L_TARGET = 0;
                        PWM_L_OVD_COUNT = 0;
                        PWM_OVD_FLAG &= 0xF0;
                    }
                    else{//残りの場合　この場合はどっちにも回転していない場合　デッドタイムを入れてブレーキ動作に移行する
                        if(DEAD_TIME_L & 0x80){//デッドタイムのカウンタが作動中の場合にデッドタイムの動作のフラグを立てる
                            DEAD_TIME_FLAG |= 0x0A;
                            DEAD_TIME_FLAG &= 0xFA;
                            PWM_L_OVD_COUNT = 0;
                            PWM_OVD_FLAG &= 0xF0;
                        }
                        else{
                            PWM_OUTPUT_FLAG |= MOTOR_1_BRAKE;
                            DEAD_TIME_FLAG |= 0x04;//PWMオフ
                            RA0PPS = 0x00;//LATxy
                            RA1PPS = 0x00;//LATxy
                            PWM_FLAG1 |= 0x01;
                            DEAD_TIME_L = DEAD_TIME;
                            PWM_L_OVD_COUNT = 0;
                            PWM_OVD_FLAG &= 0xF0;
                        }
                    }
                }
                else if(I2C_ORDER == MOTOR_R_STOP){//R ブレーキ
                    if(PWM_OUTPUT_FLAG & MOTOR_2_BRAKE);
                    else if(PWM_OUTPUT_FLAG & 0x30){
                        PWM_FLAG1 |= 0x70;
                        PWM_FLAG1 &= 0x7F;
                        PWM_FLAG2 &= 0xBF;
                        PWM_FLAG2 |= 0x40;
                        DUTY_COUNT_R = DUTY_COUNT_DOWN;
                        DUTY_R_TARGET = 0;
                        PWM_R_OVD_COUNT = 0;
                        PWM_OVD_FLAG &= 0x0F;
                    }
                    else{//残りの場合　この場合はどっちにも回転していない場合　デッドタイムを入れてブレーキ動作に移行する
                        if(DEAD_TIME_R & 0x80){//デッドタイムのカウンタが作動中の場合にデッドタイムの動作のフラグを立てる
                            DEAD_TIME_FLAG |= 0xA0;
                            DEAD_TIME_FLAG &= 0xAF;
                            PWM_R_OVD_COUNT = 0;
                            PWM_OVD_FLAG &= 0x0F;
                        }
                        else{
                            PWM_OUTPUT_FLAG |= MOTOR_2_BRAKE;
                            DEAD_TIME_FLAG |= 0x40;//PWMオフ
                            RA4PPS = 0x00;//LATxy
                            RA5PPS = 0x00;//LATxy
                            PWM_FLAG1 |= 0x10;
                            DEAD_TIME_R = DEAD_TIME;
                            PWM_R_OVD_COUNT = 0;
                            PWM_OVD_FLAG &= 0x0F;
                        }
                    }
                }
                else if(I2C_ORDER == MOTOR_L_OFF){
                    PWM3DCH = 0;
                    PWM4DCH = 0;
                    PWM3DCH_BUFF = 0;
                    PWM4DCH_BUFF = 0;
                    RA0PPS = CH1_1_PPS;
                    RA1PPS = CH1_2_PPS;
                    PWM_OUTPUT_FLAG &= 0xF0;
                    DUTY_L_NOW = 0;
                    DUTY_L_TARGET = 0;
                    DUTY_L_KEEP = 0;
                    DUTY_COUNT_L = 0;
                    PWM_FLAG1 &= 0xF0;
                    PWM_FLAG2 &= 0xF0;
                    DEAD_TIME_FLAG &= 0xF0;
                    DEAD_TIME_L = DEAD_TIME;
                    PWM_L_OVD_COUNT = 0;
                    PWM_OVD_FLAG &= 0xF0;
                }
                else if(I2C_ORDER == MOTOR_R_OFF){
                    PWM5DCH = 0;
                    PWM6DCH = 0;
                    PWM5DCH_BUFF = 0;
                    PWM6DCH_BUFF = 0;
                    RA4PPS = CH2_1_PPS;
                    RA5PPS = CH2_2_PPS;
                    PWM_OUTPUT_FLAG &= 0x0F;
                    DUTY_R_NOW = 0;
                    DUTY_R_TARGET = 0;
                    DUTY_R_KEEP = 0;
                    DUTY_COUNT_R = 0;
                    PWM_FLAG1 &= 0x0F;
                    PWM_FLAG2 &= 0x0F;
                    DEAD_TIME_FLAG &= 0x0F;
                    DEAD_TIME_R = DEAD_TIME;
                    PWM_R_OVD_COUNT = 0;
                    PWM_OVD_FLAG &= 0x0F;
                }
            }
            else{//2バイト目以降のデータ受信の場合
                if(I2C_ORDER == MOTOR_L_DUTY){//L DUTY
                    if(PWM_FLAG1 & 0x01){
                        DUTY_L_KEEP = I2C_BUFF;
                    }
                    else{
                        DUTY_L_TARGET = I2C_BUFF;
                        if(PWM_OUTPUT_FLAG & MOTOR_1_A){
                            if(PWM3DCH_BUFF < I2C_BUFF){
                                goto GO_MOTOR_1_DUTY_HI;
                            }
                            else{
                                goto GO_MOTOR_1_DUTY_LO;
                            }
                        }
                        else if(PWM_OUTPUT_FLAG & MOTOR_1_B){
                            if(PWM4DCH_BUFF < I2C_BUFF){
GO_MOTOR_1_DUTY_HI:
                                PWM_FLAG1 |= 0x08;
                                PWM_FLAG1 &= 0xF8;
                                DUTY_COUNT_L = DUTY_COUNT_UP;
                            }
                            else{
GO_MOTOR_1_DUTY_LO:
                                PWM_FLAG1 &= 0xF4;
                                DUTY_COUNT_L = DUTY_COUNT_DOWN;
                                if(I2C_BUFF == 0){
                                    PWM_FLAG1 |= 0x06;
                                    PWM_L_OVD_COUNT = 0;
                                    PWM_OVD_FLAG &= 0xF0;
                                }
                                else{
                                    PWM_FLAG1 |= 0x04;
                                }
                            }
                        }
                    }
                }
                else if(I2C_ORDER == MOTOR_R_DUTY){//R DUTY
                    if(PWM_FLAG1 & 0x10){
                        DUTY_R_KEEP = I2C_BUFF;
                    }
                    else{
                        DUTY_R_TARGET = I2C_BUFF;
                        if(PWM_OUTPUT_FLAG & MOTOR_2_A){
                            if(PWM5DCH_BUFF < I2C_BUFF){
                                goto GO_MOTOR_2_DUTY_HI;
                            }
                            else{
                                goto GO_MOTOR_2_DUTY_LO;
                            }
                        }
                        else if(PWM_OUTPUT_FLAG & MOTOR_2_B){
                            if(PWM6DCH_BUFF < I2C_BUFF){
GO_MOTOR_2_DUTY_HI:
                                PWM_FLAG1 |= 0x80;
                                PWM_FLAG1 &= 0x8F;
                                DUTY_COUNT_R = DUTY_COUNT_UP;
                            }
                            else{
GO_MOTOR_2_DUTY_LO:
                                PWM_FLAG1 &= 0x4F;
                                DUTY_COUNT_R = DUTY_COUNT_DOWN;
                                if(I2C_BUFF == 0){
                                    PWM_FLAG1 |= 0x60;
                                    PWM_R_OVD_COUNT = 0;
                                    PWM_OVD_FLAG &= 0x0F;
                                }
                                else{
                                    PWM_FLAG1 |= 0x40;
                                }
                            }
                        }
                    }
                }
                else if(I2C_ORDER == CHANGE_ODD){
                    PWM_OVD_DUTY = I2C_BUFF;
                }
            }
            I2C_COUNT ++;
        }
        CKP = 1;
        SSP1IF = 0;//SSP1IF CLEAR
    }
    else if(TMR2IF){//TIMER2 INT -----------------------------------------------
        //LCH
        //デッドタイム計算部分
        if(DEAD_TIME_L & 0x80){
            DEAD_TIME_L ++;
        }
        else if(DEAD_TIME_FLAG & 0x08){//デッドタイム後の処理を行うとき
            if(PWM_FLAG1 & 0x01){//方向逆転が完了した場合に
                DUTY_L_TARGET = DUTY_L_KEEP;
                DUTY_L_KEEP = 0;
                PWM_FLAG1 &= 0xFE;
                PWM_FLAG1 |= 0x08;
            }
            
            if(DEAD_TIME_FLAG & 0x02){//ブレーキ動作
                DEAD_TIME_FLAG &= 0xF4;//フラグリセット
                DEAD_TIME_FLAG |= 0x04;//PWMオフ
                RA0PPS = 0x00;//PWM3OUT
                RA1PPS = 0x00;//PWM4OUT
                PWM_OUTPUT_FLAG |= MOTOR_1_BRAKE;
                PWM_FLAG1 |= 0x01;
            }
            else if(DEAD_TIME_FLAG & 0x01){//逆転動作
                PWM_OUTPUT_FLAG |= MOTOR_1_B;
                DEAD_TIME_FLAG &= 0xF0;//フラグリセット
            }
            else{
                PWM_OUTPUT_FLAG |= MOTOR_1_A;
                DEAD_TIME_FLAG &= 0xF0;//フラグリセット
            }
            
            DEAD_TIME_L = DEAD_TIME;
        }
        //PWM計算
        if((DEAD_TIME_FLAG & 0x04) == 0){//PWMオフフラグが立っていないとき
            //台形制御計算部分
            if(DUTY_COUNT_L & 0x80){//台形制御を行う場合
                if(PWM_OUTPUT_FLAG & MOTOR_1_A){//正転時
                    DUTY_L_NOW = PWM3DCH_BUFF;
                    if(PWM_FLAG1 & 0x08){//増加する場合
                        if(PWM3DCH_BUFF < DUTY_L_TARGET){
                            DUTY_L_NOW += DUTY_UP;
                        }
                        if(DUTY_L_NOW >= DUTY_L_TARGET){
                            PWM3DCH_BUFF = DUTY_L_TARGET;
                            PWM_FLAG1 &= 0xF7;
                        }
                        else{
                            PWM3DCH_BUFF = (char)DUTY_L_NOW;
                            DUTY_COUNT_L = DUTY_COUNT_UP;
                        }
                    }
                    else if(PWM_FLAG1 & 0x04){//減少する場合
                        if(PWM3DCH_BUFF > DUTY_L_TARGET){
                            DUTY_L_NOW -= DUTY_DOWN;
                        }
                        if(DUTY_L_NOW <= DUTY_L_TARGET){
                            PWM3DCH_BUFF = DUTY_L_TARGET;
                            PWM_FLAG1 &= 0xFB;
                            
                            goto GO_MOTOR_1_STOP;//モーター停止処理へ
                        }
                        else{
                            PWM3DCH_BUFF = (char)DUTY_L_NOW;
                            DUTY_COUNT_L = DUTY_COUNT_DOWN;
                        }
                    }
                }
                else{//逆転時
                    DUTY_L_NOW = PWM4DCH_BUFF;
                    if(PWM_FLAG1 & 0x08){//増加する場合
                        if(PWM4DCH_BUFF < DUTY_L_TARGET){
                            DUTY_L_NOW += DUTY_UP;
                        }
                        if(DUTY_L_NOW >= DUTY_L_TARGET){
                            PWM4DCH_BUFF = DUTY_L_TARGET;
                            PWM_FLAG1 &= 0xF7;
                        }
                        else{
                            PWM4DCH_BUFF = (char)DUTY_L_NOW;
                            DUTY_COUNT_L = DUTY_COUNT_UP;
                        }
                    }
                    else if(PWM_FLAG1 & 0x04){//減少する場合
                        if(PWM4DCH_BUFF > DUTY_L_TARGET){
                            DUTY_L_NOW -= DUTY_DOWN;
                        }
                        if(DUTY_L_NOW <= DUTY_L_TARGET){
                            PWM4DCH_BUFF = DUTY_L_TARGET;
                            PWM_FLAG1 &= 0xFB;
GO_MOTOR_1_STOP://モーター停止処理
                            if(PWM_FLAG1 & 0x02){
                                PWM_OUTPUT_FLAG &= NOT_MOTOR_1_STOP;
                                DEAD_TIME_L = DEAD_TIME;
                                PWM_FLAG1 &= 0xFD;
                                if(PWM_FLAG1 & 0x01){
                                    if(PWM_FLAG2 & 0x04){//ブレーキ動作の場合
                                        DEAD_TIME_FLAG |= 0x0A;
                                        DEAD_TIME_FLAG &= 0xFA;
                                    }
                                    else{
                                        if(PWM_FLAG2 & 0x08){//逆方向の場合
                                            DEAD_TIME_FLAG |= 0x0D;
                                            DEAD_TIME_FLAG &= 0xFD;
                                        }
                                        else{
                                            DEAD_TIME_FLAG |= 0x0C;
                                            DEAD_TIME_FLAG &= 0xFC;
                                        }
                                    }
                                }
                            }
                        }
                        else{
                            PWM4DCH_BUFF = (char)DUTY_L_NOW;
                            DUTY_COUNT_L = DUTY_COUNT_DOWN;
                        }
                    }
                }
            }
            else{//カウントを行う場合
                DUTY_COUNT_L ++;
            }
            if(PWM_OVD_FLAG & OVD_EN_1){//オーバードライブを行うとき
                if(PWM_FLAG1 & 0x01){//方向切り替え中の場合
                    //オンフラグはクリアしないが，DCレジスタにそのままの値を代入する
                    PWM_OVD_FLAG &= 0xFD;
                }
                else if(((PWM_OVD_FLAG & OVD_ON_1) == 0) && (PWM_FLAG1 & 0x08)){
                    PWM_OVD_FLAG |= OVD_ON_1;
                }
                if(PWM_OVD_FLAG & OVD_ON_1){
                    if(PWM_L_OVD_COUNT){
                        if(PWM_OUTPUT_FLAG & MOTOR_1_A){
                            PWM3DCH = PWM_OVD_DUTY;
                            PWM4DCH = PWM4DCH_BUFF;
                        }
                        else if(PWM_OUTPUT_FLAG & MOTOR_1_B){
                            PWM4DCH = PWM_OVD_DUTY;
                            PWM3DCH = PWM3DCH_BUFF;
                        }
                        PWM_L_OVD_COUNT --;
                    }
                    else{
                        PWM_OVD_FLAG &= 0xFC;
                        PWM3DCH = PWM3DCH_BUFF;
                        PWM4DCH = PWM4DCH_BUFF;
                    }
                }
                else{
                    PWM3DCH = PWM3DCH_BUFF;
                    PWM4DCH = PWM4DCH_BUFF;
                }
            }
            else{
                PWM3DCH = PWM3DCH_BUFF;
                PWM4DCH = PWM4DCH_BUFF;
            }
        }
        //RCH
        //デッドタイム計算部分
        if(DEAD_TIME_R & 0x80){
            DEAD_TIME_R ++;
        }
        else if(DEAD_TIME_FLAG & 0x80){//デッドタイム後の処理を行うとき
            if(PWM_FLAG1 & 0x10){
                DUTY_R_TARGET = DUTY_R_KEEP;
                DUTY_R_KEEP = 0;
                PWM_FLAG1 &= 0xEF;
                PWM_FLAG1 |= 0x80;
            }
            
            if(DEAD_TIME_FLAG & 0x20){//ブレーキ動作
                DEAD_TIME_FLAG &= 0x4F;//フラグリセット
                DEAD_TIME_FLAG |= 0x40;//PWMオフ
                RA4PPS = 0x00;//PWM5OUT
                RA5PPS = 0x00;//PWM6OUT
                PWM_OUTPUT_FLAG |= MOTOR_2_BRAKE;
                PWM_FLAG1 |= 0x10;
            }
            else if(DEAD_TIME_FLAG & 0x10){//逆転動作
                PWM_OUTPUT_FLAG |= MOTOR_2_B;
                DEAD_TIME_FLAG &= 0x0F;//フラグリセット
            }
            else{
                PWM_OUTPUT_FLAG |= MOTOR_2_A;
                DEAD_TIME_FLAG &= 0x0F;//フラグリセット
            }
            DEAD_TIME_R = DEAD_TIME;
        }
        //PWM計算
        if((DEAD_TIME_FLAG & 0x40) == 0){//PWMオフフラグが立っていないとき
            //台形制御計算部分
            if(DUTY_COUNT_R & 0x80){//台形制御を行う場合
                if(PWM_OUTPUT_FLAG & MOTOR_2_A){
                    DUTY_R_NOW = PWM5DCH_BUFF;
                    if(PWM_FLAG1 & 0x80){//増加する場合
                        if(PWM5DCH_BUFF < DUTY_R_TARGET){
                            DUTY_R_NOW += DUTY_UP;
                        }
                        if(DUTY_R_NOW >= DUTY_R_TARGET){
                            PWM5DCH_BUFF = DUTY_R_TARGET;
                            PWM_FLAG1 &= 0x7F;
                        }
                        else{
                            PWM5DCH_BUFF = (char)DUTY_R_NOW;
                            DUTY_COUNT_R = DUTY_COUNT_UP;
                        }
                    }
                    else if(PWM_FLAG1 & 0x40){//減少する場合
                        if(PWM5DCH_BUFF > DUTY_R_TARGET){
                            DUTY_R_NOW -= DUTY_DOWN;
                        }
                        if(DUTY_R_NOW <= DUTY_R_TARGET){
                            PWM5DCH_BUFF = DUTY_R_TARGET;
                            PWM_FLAG1 &= 0xBF;
                            
                            goto GO_MOTOR_2_STOP;
                        }
                        else{
                            PWM5DCH_BUFF = (char)DUTY_R_NOW;
                            DUTY_COUNT_R = DUTY_COUNT_DOWN; 
                        }
                    }
                }
                else{
                    DUTY_R_NOW = PWM6DCH_BUFF;
                    if(PWM_FLAG1 & 0x80){//増加する場合
                        if(PWM6DCH_BUFF < DUTY_R_TARGET){
                            DUTY_R_NOW += DUTY_UP;
                        }
                        if(DUTY_R_NOW >= DUTY_R_TARGET){
                            PWM6DCH_BUFF = DUTY_R_TARGET;
                            PWM_FLAG1 &= 0x7F;
                        }
                        else{
                            PWM6DCH_BUFF = (char)DUTY_R_NOW;
                        }
                        DUTY_COUNT_R = DUTY_COUNT_UP;
                    }
                    else if(PWM_FLAG1 & 0x40){//減少する場合
                        if(PWM6DCH_BUFF > DUTY_R_TARGET){
                            DUTY_R_NOW -= DUTY_DOWN;
                        }
                        if(DUTY_R_NOW <= DUTY_R_TARGET){
                            PWM6DCH_BUFF = DUTY_R_TARGET;
                            PWM_FLAG1 &= 0xBF;
GO_MOTOR_2_STOP:
                            if(PWM_FLAG1 & 0x20){
                                PWM_OUTPUT_FLAG &= NOT_MOTOR_2_STOP;
                                DEAD_TIME_R = DEAD_TIME;
                                PWM_FLAG1 &= 0xDF;
                                if(PWM_FLAG1 & 0x10){
                                    if(PWM_FLAG2 & 0x40){
                                        DEAD_TIME_FLAG |= 0xA0;
                                        DEAD_TIME_FLAG &= 0xAF;
                                    }
                                    else{
                                        if(PWM_FLAG2 & 0x80){//逆方向の場合
                                            DEAD_TIME_FLAG |= 0xD0;
                                            DEAD_TIME_FLAG &= 0xDF;
                                        }
                                        else{
                                            DEAD_TIME_FLAG |= 0xC0;
                                            DEAD_TIME_FLAG &= 0xCF;
                                        }
                                    }
                                }
                            }
                        }
                        else{
                            PWM6DCH_BUFF = (char)DUTY_R_NOW;
                            DUTY_COUNT_R = DUTY_COUNT_DOWN; 
                        }
                    }
                }
            }
            else{//カウントを行う場合
                DUTY_COUNT_R ++;
            }
            if(PWM_OVD_FLAG & OVD_ON_2){//オーバードライブを行うとき
                if(PWM_FLAG1 & 0x10){//方向切り替え中の場合
                    //オンフラグはクリアしないが，DCレジスタにそのままの値を代入する
                    PWM_OVD_FLAG &= 0xFD;
                }
                else if(((PWM_OVD_FLAG & 0x20) == 0) && (PWM_FLAG1 & 0x80)){
                    PWM_OVD_FLAG |= 0x20;
                }
                if(PWM_OVD_FLAG & 0x20){
                    if(PWM_R_OVD_COUNT){
                        if(PWM_OUTPUT_FLAG & MOTOR_2_A){
                            PWM5DCH = PWM_OVD_DUTY;
                            PWM6DCH = PWM6DCH_BUFF;
                        }
                        else if(PWM_OUTPUT_FLAG & MOTOR_2_B){
                            PWM6DCH = PWM_OVD_DUTY;
                            PWM5DCH = PWM5DCH_BUFF;
                        }
                        PWM_R_OVD_COUNT --;
                    }
                    else{//カウントが完了したタイミングでフラグをクリアする
                        PWM_OVD_FLAG &= 0x0F;
                        PWM5DCH = PWM5DCH_BUFF;
                        PWM6DCH = PWM6DCH_BUFF;
                    }
                }
                else{
                    PWM5DCH = PWM3DCH_BUFF;
                    PWM6DCH = PWM4DCH_BUFF;
                }
            }
            else{
                PWM5DCH = PWM5DCH_BUFF;
                PWM6DCH = PWM6DCH_BUFF;
            }
        }
        TMR2IF = 0;
    }
}