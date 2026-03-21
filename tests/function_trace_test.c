
void f1() {
    int size = 50;
    int array[size];

    for (int i = 0; i < size; i++) {
        array[i] = i;
        for (int b = i - 1; b >= 0; b--) {
            array[i] += array[b];
        }
    }

}

int f3() {
    sleep(1);
}


int f2() {
    for (int i = 0; i < 10; i++) {
        f3();
        sleep(1);
        f1();
    }
}



int main() {
    f2();
    return 0;
}