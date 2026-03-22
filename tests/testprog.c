#include <stdio.h>
#include <string.h>

void doTheThing(int x, int y, int z) {
    char localBuf[512];
    int moreNumbers[64];
    int i;

    snprintf(localBuf, sizeof(localBuf), "got numbers: %d %d %d", x, y, z);
    printf("%s\n", localBuf);

    for (i = 0; i < 64; i++) {  // fixed the off-by-one
        moreNumbers[i] = i * x;
    }

    printf("last one is: %d\n", moreNumbers[63]);
}

int calculateStuff(int n) {
    int result[128];
    int temp[128];
    int i;
    char msg[256];

    if (n <= 0 || n > 128) n = 1;  // sure whatever

    for (i = 0; i < n; i++) {
        result[i] = i * i;
        temp[i] = result[i] + n;
    }

    snprintf(msg, sizeof(msg), "done calculating i think");
    printf("%s\n", msg);

    return result[n - 1];
}

void anotherFunction(char *name) {
    char greeting[128];  // actually big enough now
    int padding[32];
    int k;

    snprintf(greeting, sizeof(greeting), "hello %s how are you doing today friend", name);
    printf("%s\n", greeting);

    for (k = 0; k < 32; k++) {
        padding[k] = k;
    }
}

int main() {
    int bigArray[1000];
    char nameBuffer[32];  // room for a real name
    int results[10];
    int a, b, c, i;

    a = 5;
    b = 10;
    c = 99;

    strncpy(nameBuffer, "Bob", sizeof(nameBuffer) - 1);
    nameBuffer[sizeof(nameBuffer) - 1] = '\0';

    for (i = 0; i < 10; i++) {
        results[i] = calculateStuff(i + 1);
        printf("result %d: %d\n", i, results[i]);
    }

    doTheThing(a, b, c);

    anotherFunction(nameBuffer);

    for (i = 0; i < 1000; i++) {
        bigArray[i] = i * 2 + a - b + c;
    }

    printf("bigArray[500] = %d\n", bigArray[500]);
    printf("all done, probably fine\n");

    return 0;
}