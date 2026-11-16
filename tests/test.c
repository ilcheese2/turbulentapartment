#include <math.h>
#include <stdio.h>

extern double __enzyme_autodiff(void *function, ...);
static int checks = 0;
void check(double actual, double expected) {
    double tolerance = 1e-5;
    if (fabs(actual - expected) > tolerance * (1.0 + fabs(expected))) {
        printf("fail %d got %.12g, expected %.12g\n", ++checks, actual, expected);
    } else {
        printf("pass %d\n", ++checks);
    }
}

double polynomial(double x) {
    return 3 * x * x - 2 * x;
}

double select(double x) {
    if (x > 0.0)
        return x * x;
    return 3.0 * x;
}

double loop(double x, int n) {
    double result = 1.0;

    for (int i = 0; i < n; ++i) {
        result *= x;
    }

    return result;
}

double dotproduct(double* x, double* y, int n) {
    double sum = 0.0;

    for (int i = 0; i < n; i++) {
        sum += x[i] * y[i];
    }

    return sum;
}

double overwrite(double* a, double x) { // zeroed
    a[0] = x;
    a[0] = a[0] * a[0];
    a[0] = a[0] + x;
    return a[0]; // x * x + x 2x + 1
}

void voidoutput(double* x, double* out) {
    *out = *x * *x + 2.0;
}

double floatdouble(double x) {
    float xf = (float)x;
    float square = xf * xf;
    return (double)square + x;
}

int main() {
    check(__enzyme_autodiff(polynomial, 2.0), 10.0);
    check(__enzyme_autodiff(select, 2.0), 4.0);
    check(__enzyme_autodiff(select, -2.0), 3.0);
    check(__enzyme_autodiff(loop, 1.0, 1), 1.0);
    check(__enzyme_autodiff(loop, 3.0, 2), 6.0);
    double x[] = {1.0, -2.0, 3.0};
    double y[] = {4.0, 5.0, -6.0};
    double dx[] = {0.0, 0.0, 0.0};
    double dy[] = {0.0, 0.0, 0.0};
    __enzyme_autodiff(dotproduct, x, dx, y, dy, 3);
    check(dx[0], 4.0);
    check(dx[1], 5.0);
    check(dx[2], -6.0);
    check(dy[0], 1.0);
    check(dy[1], -2.0);
    check(dy[2], 3.0);
    __enzyme_autodiff(dotproduct, x, dx, y, dy, 3); // no zero
    check(dx[0], 8.0);
    check(dx[1], 10.0);
    check(dx[2], -12.0);
    check(dy[0], 2.0);
    check(dy[1], -4.0);
    check(dy[2], 6.0);
    dy[0] = 0.0;
    check(__enzyme_autodiff(overwrite, y, dy, 4.0), 9.0); // technically should have option to not
    x[0] = 5.0;
    dx[0] = 0.0;
    dy[0] = 7.0;
    __enzyme_autodiff(voidoutput, x, dx, y, dy);
    check(dx[0], 70.0); // 7 * 2(5)
    check(y[0], 27.0); // 5(5) + 2
    check(__enzyme_autodiff(floatdouble, -1.5), -2.0); // useless
}