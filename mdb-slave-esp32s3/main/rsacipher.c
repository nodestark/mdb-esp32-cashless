#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Function to calculate the GCD (Greatest Common Divisor)
int mdc(int a, int b) {
    while (b != 0) {
        int temp = b;
        b = a % b;
        a = temp;
    }
    return a;
}

// Function to calculate the modular inverse of 'e' mod 'phi'
int modInverse(int e, int phi) {
    int d = 1;
    while ((d * e) % phi != 1) {
        d++;
    }
    return d;
}

// Function for modular exponentiation (C = M^e mod n)
long long int modExp(long long int base, int exp, int mod) {
    long long int result = 1;
    base = base % mod;
    while (exp > 0) {
        if (exp % 2 == 1)  // If odd, multiply by the result
            result = (result * base) % mod;
        exp = exp >> 1; // Divide exp by 2
        base = (base * base) % mod;
    }
    return result;
}

int main() {
    // We choose two small prime numbers for simplicity
    int p = 61, q = 53;
    int n = p * q;
    int phi = (p - 1) * (q - 1);

    // Choosing an 'e' coprime to 'phi'
    int e = 3;
    while (mdc(e, phi) != 1) {
        e++;
    }

    // Calculating 'd', the modular inverse of 'e' mod 'phi'
    int d = modInverse(e, phi);

    printf(" Public Key (e, n): (%d, %d)\n", e, n);
    printf(" Private Key (d, n): (%d, %d)\n", d, n);

    // Message to encrypt
    int mensagem = 42;
    printf("\n Original Message: %d\n", mensagem);

    // Encrypt
    int criptografado = modExp(mensagem, e, n);
    printf(" Encrypted Message: %d\n", criptografado);

    // Decrypt
    int descriptografado = modExp(criptografado, d, n);
    printf(" Decrypted Message: %d\n", descriptografado);

    return 0;
}
