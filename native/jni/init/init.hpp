#include <utils.hpp>

using kv_pairs = std::vector<std::pair<std::string, std::string>>;

// For API 28 AVD, it uses legacy SAR setup that requires
// special hacks in magiskinit to work properly. We do not
// necessarily want this enabled in production builds.
#define ENABLE_AVD_HACK 0

struct BootConfig {
    bool skip_initramfs;
    bool force_normal_boot;
    bool rootwait;
    bool emulator;
    char slot[3];
    char dt_dir[64];
    char fstab_suffix[32];
    char hardware[32];
    char hardware_plat[32];

    void set(const kv_pairs &);
    void print();
};

struct fstab_entry {
    std::string dev;
    std::string mnt_point;
    std::string type;
    std::string mnt_flags;
    std::string fsmgr_flags;

    fstab_entry() = default;
    fstab_entry(const fstab_entry &) = delete;
    fstab_entry(fstab_entry &&) = default;
    fstab_entry &operator=(const fstab_entry&) = delete;
    fstab_entry &operator=(fstab_entry&&) = default;
};

#define INIT_SOCKET "MAGISKINIT"
#define DEFAULT_DT_DIR "/proc/device-tree/firmware/android"

extern std::vector<std::string> mount_list;

bool unxz(int fd, const uint8_t *buf, size_t size);
void load_kernel_info(BootConfig *config);
bool check_two_stage();
void setup_klog();
const char *backup_init();

/***************
 * Base classes
 ***************/

class BaseInit {
protected:
    BootConfig *config = nullptr;
    char **argv = nullptr;

    [[noreturn]] void exec_init();
    void read_dt_fstab(std::vector<fstab_entry> &fstab);
public:
    BaseInit(char *argv[], BootConfig *config = nullptr) : config(config), argv(argv) {}
    virtual ~BaseInit() = default;
    virtual void start() = 0;
};

class MagiskInit : public BaseInit {
protected:
    mmap_data self;
    mmap_data magisk_cfg;
    std::string custom_rules_dir;

#if ENABLE_AVD_HACK
    // When this boolean is set, this means we are currently
    // running magiskinit on legacy SAR AVD emulator
    bool avd_hack = false;
#else
    // Make it const so compiler can optimize hacks out of the code
    static const bool avd_hack = false;
#endif

    void mount_with_dt();
    bool patch_sepolicy(const char *file);
    void setup_tmp(const char *path);
    void mount_rules_dir(const char *dev_base, const char *mnt_base);
    void patch_rw_root();
public:
    using BaseInit::BaseInit;
};

class SARBase : public MagiskInit {
protected:
    std::vector<raw_file> overlays;

    void backup_files();
    void patch_ro_root();
    void mount_system_root();
public:
    using MagiskInit::MagiskInit;
};

/***************
 * 2 Stage Init
 ***************/

class FirstStageInit : public BaseInit {
private:
    void prepare();
public:
    FirstStageInit(char *argv[], BootConfig *config) : BaseInit(argv, config) {
        LOGD("%s\n", __FUNCTION__);
    };
    void start() override {
        prepare();
        exec_init();
    }
};

class SecondStageInit : public SARBase {
private:
    bool prepare();
public:
    SecondStageInit(char *argv[]) : SARBase(argv) {
        setup_klog();
        LOGD("%s\n", __FUNCTION__);
    };

    void start() override {
        if (prepare())
            patch_rw_root();
        else
            patch_ro_root();
        exec_init();
    }
};

/*************
 * Legacy SAR
 *************/

class LegacySARInit : public SARBase {
private:
    bool early_mount();
    void first_stage_prep();
public:
    LegacySARInit(char *argv[], BootConfig *config) : SARBase(argv, config) {
        LOGD("%s\n", __FUNCTION__);
    };
    void start() override {
        if (early_mount())
            first_stage_prep();
        else
            patch_ro_root();
        exec_init();
    }
};

/************
 * Initramfs
 ************/

class RootFSInit : public MagiskInit {
private:
    void early_mount();
public:
    RootFSInit(char *argv[], BootConfig *config) : MagiskInit(argv, config) {
        LOGD("%s\n", __FUNCTION__);
    }
    void start() override {
        early_mount();
        patch_rw_root();
        exec_init();
    }
};

class MagiskProxy : public MagiskInit {
public:
    explicit MagiskProxy(char *argv[]) : MagiskInit(argv) {
        LOGD("%s\n", __FUNCTION__);
    }
    void start() override;
};
