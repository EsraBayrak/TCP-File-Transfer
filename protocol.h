#include <stdio.h>
#include <string.h>

int server_main(void);
int client_main(void);

static void usage(const char* exe) {
    printf("Usage:\n");
    printf("  %s server\n", exe);
    printf("  %s client\n", exe);
    printf("\nDev-C++: Execute -> Parameters -> yaz: server veya client\n");
}

static void flush_line(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {}
}

int main(int argc, char** argv) {

    if (argc >= 2) {
        if (strcmp(argv[1], "server") == 0) return server_main();
        if (strcmp(argv[1], "client") == 0) return client_main();

        printf("Unknown mode: %s\n\n", argv[1]);
        usage(argv[0]);
        printf("Press Enter to exit...\n");
        getchar();
        return 0;
    }

  
    while (1) {
        int choice = 0;
        printf("\n=== TCP_FileTransfer ===\n");
        printf("1) Server\n");
        printf("2) Client\n");
        printf("3) Exit\n"); 
        printf("Select: ");
        fflush(stdout);

        if (scanf("%d", &choice) != 1) {
            flush_line();
            continue;
        }
        flush_line();

        if (choice == 1) return server_main();
        if (choice == 2) return client_main();
        if (choice == 3) return 0;
    }
}


