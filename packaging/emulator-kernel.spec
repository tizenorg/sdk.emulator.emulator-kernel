%ifarch %{arm}
%define config_name arm_tizen_emul_defconfig
%define buildarch arm
%define imageName zImage
%else
%define config_name i386_tizen_emul_defconfig
%define buildarch x86
%define imageName bzImage
%endif

%define abiver 1
%define build_id %{config_name}.%{abiver}

#%undefine _missing_build_ids_terminate_build
Name: emulator-kernel
Summary: The Linux Emulator Kernel
Version: 3.14.25
Release: 1
License: GPL-2.0
Group: System Environment/Kernel
Vendor: The Linux Community
URL: http://www.kernel.org
Source0:   %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{PACKAGE_VERSION}-root
ExclusiveArch: %{ix86}

%define fullVersion %{version}

#BuildRequires: linux-glibc-devel
#BuildRequires: bc
BuildRequires: emulator-kernel-user-headers

Provides: kernel = %{version}-%{release}
Provides: kernel-uname-r = %{fullVersion}

%description
The Linux Kernel, the operating system core itself

%package user-headers
Summary: Header files for the Linux kernel for use by glibc
Group: Development/System
Obsoletes: kernel-headers
Provides: kernel-headers = %{version}-%{release}

%description user-headers
Kernel-headers includes the C header files that specify the interface
between the Linux kernel and userspace libraries and programs.  The
header files define structures and constants that are needed for
building most standard programs and are also needed for rebuilding the
glibc package.

%package devel
Summary: Prebuilt linux kernel for out-of-tree modules
Group: Development/System
Provides: kernel-devel = %{fullVersion}
Provides: kernel-devel-uname-r = %{fullVersion}
Requires: %{name} = %{version}-%{release}

%description devel
Prebuilt linux kernel for out-of-tree modules.

%prep
%setup -q

%build
# 1. Compile sources
make %{config_name}
#make EXTRAVERSION="-%{build_id}" %{?_smp_mflags}

# 2. Build uImage
#make EXTRAVERSION="-%{build_id}" %{imageName} %{?_smp_mflags}

# 3. Build modules
make modules %{?_smp_mflags}

# 4. Create tar repo for build directory
tar cpSf linux-kernel-build-%{fullVersion}.tar .

%install
QA_SKIP_BUILD_ROOT="DO_NOT_WANT"; export QA_SKIP_BUILD_ROOT

# 1. Destynation directories
mkdir -p %{buildroot}/usr/src/linux-kernel-build-%{fullVersion}
mkdir -p %{buildroot}/lib/modules/%{fullVersion}
mkdir -p %{buildroot}/boot/

# 2. Install uImage, System.map, ...
#install -m 755 arch/%{buildarch}/boot/%{imageName} %{buildroot}/boot/
#install -m 644 System.map %{buildroot}/boot/System.map-%{fullVersion}
install -m 644 .config %{buildroot}/boot/config-%{fullVersion}

# 3. Install modules
make INSTALL_MOD_STRIP=1 INSTALL_MOD_PATH=%{buildroot} modules_install

# 4. Install kernel headers
make INSTALL_PATH=%{buildroot} INSTALL_MOD_PATH=%{buildroot} INSTALL_HDR_PATH=%{buildroot}/usr headers_install

# 5. Restore source and build irectory
tar -xf linux-kernel-build-%{fullVersion}.tar -C %{buildroot}/usr/src/linux-kernel-build-%{fullVersion}
mv %{buildroot}/usr/src/linux-kernel-build-%{fullVersion}/arch/%{buildarch} .
mv %{buildroot}/usr/src/linux-kernel-build-%{fullVersion}/arch/Kconfig .
rm -rf %{buildroot}/usr/src/linux-kernel-build-%{fullVersion}/arch/*
mv %{buildarch} %{buildroot}/usr/src/linux-kernel-build-%{fullVersion}/arch/
mv Kconfig      %{buildroot}/usr/src/linux-kernel-build-%{fullVersion}/arch/

# 6. Remove files
find %{buildroot}/usr/src/linux-kernel-build-%{fullVersion} -name ".tmp_vmlinux*" -exec rm -f {} \;
find %{buildroot}/usr/src/linux-kernel-build-%{fullVersion} -name "\.*dtb*tmp" -exec rm -f {} \;
find %{buildroot}/usr/src/linux-kernel-build-%{fullVersion} -name "*\.*tmp" -exec rm -f {} \;
find %{buildroot}/usr/src/linux-kernel-build-%{fullVersion} -name "vmlinux" -exec rm -f {} \;
find %{buildroot}/usr/src/linux-kernel-build-%{fullVersion} -name "bzImage" -exec rm -f {} \;
find %{buildroot}/usr/src/linux-kernel-build-%{fullVersion} -name "zImage" -exec rm -f {} \;
find %{buildroot}/usr/src/linux-kernel-build-%{fullVersion} -name "*.cmd" -exec rm -f {} \;
find %{buildroot}/usr/src/linux-kernel-build-%{fullVersion} -name "*\.ko" -exec rm -f {} \;
find %{buildroot}/usr/src/linux-kernel-build-%{fullVersion} -name "*\.o" -exec rm -f {} \;
find %{buildroot}/usr/src/linux-kernel-build-%{fullVersion} -name "*\.S" -exec rm -f {} \;
find %{buildroot}/usr/src/linux-kernel-build-%{fullVersion} -name "*\.c" -not -path "%{buildroot}/usr/src/linux-kernel-build-%{fullVersion}/scripts/*" -exec rm -f {} \;
find %{buildroot}/usr/include -name "\.\.install.cmd"  -exec rm -f {} \;
find %{buildroot}/usr/include -name "\.install"  -exec rm -f {} \;
find %{buildroot}/usr -name "..install.cmd" -exec rm -f {} \;

rm -rf %{buildroot}/boot/vmlinux*
rm -rf %{buildroot}/System.map*
rm -rf %{buildroot}/vmlinux*

# 7. Create symbolic links
rm -f %{buildroot}/lib/modules/%{fullVersion}/build
rm -f %{buildroot}/lib/modules/%{fullVersion}/source
ln -sf /usr/src/linux-kernel-build-%{fullVersion} %{buildroot}/lib/modules/%{fullVersion}/build

find %{buildroot}/lib/modules/ -name "*.ko" -type f -exec chmod 755 {} \;

%clean
rm -rf %{buildroot}

%files user-headers
%defattr (-, root, root)
/usr/include

%files devel
%defattr (-, root, root)
/usr/src/linux-kernel-build-%{fullVersion}
/lib/modules/%{fullVersion}/modules.*
/lib/modules/%{fullVersion}/build

%files
#%license COPYING
#/boot/%{imageName}
#/boot/System.map*
/boot/config*
/lib/modules/%{fullVersion}/kernel
/lib/modules/%{fullVersion}/modules.*
