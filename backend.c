#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

int DRY_RUN = 1;  /* stays 1 unless --real is given */

void run(const char *fmt, ...) {
    char cmd[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(cmd, sizeof cmd, fmt, a);
    va_end(a);
    printf("RUN: %s\n", cmd);
    fflush(stdout);
    if (!DRY_RUN) system(cmd);
}

int ok_user(const char *s) {
    if (!*s) return 0;
    for (; *s; s++)
        if (!(islower((unsigned char)*s) || isdigit((unsigned char)*s)
              || *s == '_' || *s == '-')) return 0;
    return 1;
}

int ok_disk(const char *s) {
    if (strncmp(s, "/dev/", 5) != 0) return 0;
    for (; *s; s++)
        if (!(isalnum((unsigned char)*s) || *s == '/')) return 0;
    return 1;
}

int main(int argc, char **argv) {
    char pass[128], efi[64], root[64];

    if (argc == 4 && strcmp(argv[3], "--real") == 0) {
        DRY_RUN = 0;
    } else if (argc != 3) {
        printf("Usage: ./backend USERNAME DISK [--real]\n");
        return 1;
    }
    const char *user = argv[1];
    const char *disk = argv[2];

    if (!ok_user(user)) { printf("Bad username\n"); return 1; }
    if (!ok_disk(disk)) { printf("Bad disk\n"); return 1; }

    /* password comes in through stdin, not the command line */
    if (!fgets(pass, sizeof pass, stdin)) { printf("No password\n"); return 1; }
    pass[strcspn(pass, "\r\n")] = '\0';
    if (!*pass || strchr(pass, '\'')) { printf("Bad password\n"); return 1; }

    /* NVMe and SD disks need a "p" before the number */
    const char *p = isdigit((unsigned char)disk[strlen(disk) - 1]) ? "p" : "";
    snprintf(efi, sizeof efi, "%s%s1", disk, p);
    snprintf(root, sizeof root, "%s%s2", disk, p);

    printf("Backend started (%s)\n", DRY_RUN ? "DRY RUN" : "REAL");
    printf("User: %s | Disk: %s\n\n", user, disk);

    printf("1. Partition\n");
    run("parted -s %s mklabel gpt", disk);
    run("parted -s %s mkpart ESP fat32 1MiB 513MiB", disk);
    run("parted -s %s set 1 esp on", disk);
    run("parted -s %s mkpart root ext4 513MiB 100%%", disk);

    printf("\n2. Format\n");
    run("mkfs.fat -F32 %s", efi);
    run("mkfs.ext4 -F %s", root);

    printf("\n3. Mount\n");
    run("mount %s /mnt", root);
    run("mkdir -p /mnt/boot");
    run("mount %s /mnt/boot", efi);

    printf("\n4. Copy system from the ISO\n");
    run("rsync -aAXH /run/archiso/airootfs/ /mnt/");
    run("genfstab -U /mnt >> /mnt/etc/fstab");

    printf("\n5. Bootloader\n");
    run("arch-chroot /mnt mkinitcpio -P");
    run("arch-chroot /mnt grub-install --target=x86_64-efi "
        "--efi-directory=/boot --bootloader-id=GRUB");
    run("arch-chroot /mnt grub-mkconfig -o /boot/grub/grub.cfg");

    printf("\n6. Create user\n");
    run("arch-chroot /mnt useradd -m -G wheel %s", user);
    printf("RUN: echo '%s:****' | arch-chroot /mnt chpasswd\n", user);
    if (!DRY_RUN) {
        char cmd[512];
        snprintf(cmd, sizeof cmd,
                 "echo '%s:%s' | arch-chroot /mnt chpasswd", user, pass);
        system(cmd);
    }

    printf("\n7. Desktop on boot\n");
    run("arch-chroot /mnt systemctl enable lightdm");

    printf("\nFinished.\n");
    return 0;
}
