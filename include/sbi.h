struct sbiret {
    long error;
    long value;
};

struct sbiret sbi_ecall(int ext,
                        int fid,
                        unsigned long arg0,
                        unsigned long arg1,
                        unsigned long arg2,
                        unsigned long arg3,
                        unsigned long arg4,
                        unsigned long arg5);
                        
long sbi_get_spec_version(void);
long sbi_probe_extension(int extid);
long sbi_get_impl_id(void);
long sbi_get_impl_version(void);
void sbi_set_timer(unsigned long stime_value);
