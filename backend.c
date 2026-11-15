#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>

int DRY = 0;

void step(const char *fmt, ...) {
    char cmd[1024];
    va_list a;
    va_start(a, fmt);
    vsnprintf(cmd, sizeof cmd, fmt, a);
    va_end(a);
    printf("RUN: %s\n", cmd);
    fflush(stdout);
    if (DRY) return;
    if (system(cmd) != 0) {
        printf("FAILED: %s\n", cmd);
        exit(1);
    }
}

int ok_user(const char *s) {
    if (!*s || strlen(s) > 32) return 0;
    for (; *s; s++)
        if (!(islower((unsigned char)*s) || isdigit((unsigned char)*s)
              || *s == '_' || *s == '-')) return 0;
    return 1;
}

int ok_disk(const char *s) {
    if (strncmp(s, "/dev/", 5) != 0 || strlen(s) > 40) return 0;
    for (; *s; s++)
        if (!(isalnum((unsigned char)*s) || *s == '/')) return 0;
    return 1;
}

int is_block(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISBLK(st.st_mode);
}

int disk_in_use(const char *disk) {
    FILE *f = fopen("/proc/mounts", "r");
    char line[512];
    int used = 0;
    if (!f) return 1;
    while (fgets(line, sizeof line, f))
        if (strncmp(line, disk, strlen(disk)) == 0) used = 1;
    fclose(f);
    return used;
}

/* Ask Linux for the real partition names (works for every disk type) */
int find_parts(const char *disk, char *efi, char *root, size_t n) {
    char cmd[256], line[256];
    int found = 0;
    snprintf(cmd, sizeof cmd, "lsblk -lnpo NAME,TYPE %s", disk);
    FILE *f = popen(cmd, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char name[128], type[32];
        if (sscanf(line, "%127s %31s", name, type) == 2
            && strcmp(type, "part") == 0) {
            if (found == 0) snprintf(efi, n, "%s", name);
            else if (found == 1) snprintf(root, n, "%s", name);
            found++;
        }
    }
    pclose(f);
    return found >= 2;
}

int main(int argc, char **argv) {
    char pass[128], efi[128] = "", root[128] = "";
    const char *user = NULL, *disk = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dry-run") == 0) DRY = 1;
        else if (strcmp(argv[i], "--real") == 0) DRY = 0;
        else if (!user) user = argv[i];
        else if (!disk) disk = argv[i];
        else { printf("Too many arguments\n"); return 1; }
    }
    if (!user || !disk) {
        printf("Usage: ./backend USERNAME DISK [--dry-run]\n");
        return 1;
    }
    if (!ok_user(user)) { printf("Bad username\n"); return 1; }
    if (!ok_disk(disk)) { printf("Bad disk name\n"); return 1; }

    if (!DRY) {
        if (geteuid() != 0) { printf("Run as root (sudo)\n"); return 1; }
        if (!is_block(disk)) { printf("%s is not a disk\n", disk); return 1; }
        if (disk_in_use(disk)) {
            printf("%s is mounted. Refusing.\n", disk);
            return 1;
        }
    }

    if (!fgets(pass, sizeof pass, stdin)) { printf("No password\n"); return 1; }
    pass[strcspn(pass, "\r\n")] = '\0';
    if (!*pass) { printf("Empty password\n"); return 1; }

    printf("Backend started (%s)\n", DRY ? "DRY RUN" : "REAL");
    printf("User: %s | Disk: %s\n\n", user, disk);

    printf("1. Partition\n");
    step("wipefs -a %s", disk);
    step("parted -s %s mklabel gpt", disk);
    step("parted -s %s mkpart ESP fat32 1MiB 513MiB", disk);
    step("parted -s %s set 1 esp on", disk);
    step("parted -s %s mkpart root ext4 513MiB 100%%", disk);
    step("partprobe %s", disk);
    step("udevadm settle");
    step("sleep 2");

    /* Find the real partition names */
    if (DRY) {
        snprintf(efi, sizeof efi, "%s1", disk);
        snprintf(root, sizeof root, "%s2", disk);
    } else if (!find_parts(disk, efi, root, sizeof efi)) {
        printf("FAILED: could not find the new partitions\n");
        return 1;
    }
    printf("Boot partition: %s\nRoot partition: %s\n", efi, root);

    printf("\n2. Format\n");
    step("mkfs.fat -F32 %s", efi);
    step("mkfs.ext4 -F %s", root);

    printf("\n3. Mount\n");
    step("mount %s /mnt", root);
    step("mkdir -p /mnt/boot");
    step("mount %s /mnt/boot", efi);

    printf("\n4. Copy system from the ISO\n");
    step("rsync -aAXH --exclude=/proc/* --exclude=/sys/* --exclude=/dev/* "
         "--exclude=/run/* --exclude=/tmp/* --exclude=/mnt/* "
         "/run/archiso/airootfs/ /mnt/");
    step("genfstab -U /mnt > /mnt/etc/fstab");

    printf("\n5. Clean live-only settings\n");
    step("rm -f /mnt/etc/mkinitcpio.conf.d/archiso.conf");
    step("rm -f /mnt/etc/systemd/system/getty@tty1.service.d/autologin.conf");

    printf("\n6. Bootloader\n");
    step("arch-chroot /mnt mkinitcpio -P");
    step("arch-chroot /mnt grub-install --target=x86_64-efi "
         "--efi-directory=/boot --bootloader-id=GRUB --removable");
    step("arch-chroot /mnt grub-mkconfig -o /boot/grub/grub.cfg");

    printf("\n7. Create user\n");
    step("arch-chroot /mnt useradd -m -G wheel %s", user);
    printf("RUN: set password for %s\n", user);
    if (!DRY) {
        FILE *c = popen("arch-chroot /mnt chpasswd", "w");
        if (!c) { printf("FAILED: chpasswd\n"); return 1; }
        fprintf(c, "%s:%s\n", user, pass);
        if (pclose(c) != 0) { printf("FAILED: chpasswd\n"); return 1; }
    }

    printf("\n8. Desktop on boot\n");
    step("arch-chroot /mnt systemctl enable lightdm");

    printf("\n9. Finish\n");
    step("sync");
    step("umount -R /mnt");

    printf("\nFinished.\n");
    return 0;
}
