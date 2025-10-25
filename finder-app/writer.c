#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>

int main(int argc, char *argv[]) {
    // Setup syslog logging
    openlog("writer", LOG_PID, LOG_USER);
    
    // Check if correct number of arguments provided
    if (argc != 3) {
        syslog(LOG_ERR, "Error: Invalid number of arguments. Usage: writer <file> <string>");
        fprintf(stderr, "Error: Invalid number of arguments. Usage: writer <file> <string>\n");
        closelog();
        return 1;
    }
    
    char *writefile = argv[1];
    char *writestr = argv[2];
    
    // Log the writing operation
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);
    
    // Open file for writing
    FILE *file = fopen(writefile, "w");
    if (file == NULL) {
        syslog(LOG_ERR, "Error: Could not create file %s", writefile);
        perror("Error creating file");
        closelog();
        return 1;
    }
    
    // Write string to file
    if (fprintf(file, "%s\n", writestr) < 0) {
        syslog(LOG_ERR, "Error: Could not write to file %s", writefile);
        fclose(file);
        closelog();
        return 1;
    }
    
    // Close file
    fclose(file);
    
    // Close syslog
    closelog();
    
    return 0;
}
