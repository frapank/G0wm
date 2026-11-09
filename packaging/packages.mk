.PHONY: stage tarball deb rpm packages

PKGVER   := $(subst -,+,$(patsubst v%,%,$(VERSION)))
PKGDIR   = $(BUILDDIR)/pkg
PKGROOT  = $(PKGDIR)/root
LICDIR   = $(PKGROOT)/usr/share/licenses/g0wm
DEBDIR   = $(PKGDIR)/debian
DEBROOT  = $(DEBDIR)/g0wm
MACHINE := $(shell uname -m)
DEB_ARCH ?= $(shell dpkg --print-architecture 2>/dev/null || echo amd64)
MAINTAINER ?= $(shell git config user.name) <$(shell git config user.email)>
# Empty means use dpkg-shlibdeps.
DEB_DEPENDS ?=

stage: g0wm
	@printf '  STAGE   %s\n' '$(PKGVER)'
	$(Q)rm -rf $(PKGROOT)
	$(Q)$(MAKE) --no-print-directory install DESTDIR=$(abspath $(PKGROOT)) PREFIX=/usr
	$(Q)strip $(PKGROOT)/usr/bin/g0wm
	$(Q)install -Dm644 -t $(LICDIR) packaging/copyright LICENSES/*.txt
ifeq ($(BUNDLE_DEPS),yes)
	$(Q)for lib in wlroots libscfg; do \
		install -Dm644 subprojects/prefix/share/licenses/$$lib/LICENSE $(LICDIR)/$$lib/LICENSE; \
	done
endif
	@# A restrictive umask must not end up in the package.
	$(Q)find $(PKGROOT) -type d -exec chmod 755 {} +

# Unlike deb/rpm, this does not check runtime dependencies.
tarball: stage
	$(Q)tar --owner=0 --group=0 --numeric-owner -C $(PKGROOT) \
		-czf $(PKGDIR)/g0wm-$(PKGVER)-linux-$(MACHINE).tar.gz usr
	@printf '  TAR     %s\n' '$(PKGDIR)/g0wm-$(PKGVER)-linux-$(MACHINE).tar.gz'

deb: stage
	@printf '  DEB     %s\n' '$(PKGVER)'
	$(Q)rm -rf $(DEBDIR)
	$(Q)mkdir -p $(DEBDIR)
	$(Q)cp -a $(PKGROOT) $(DEBROOT)
	$(Q)install -Dm644 packaging/copyright $(DEBROOT)/usr/share/doc/g0wm/copyright
	$(Q)install -d -m755 $(DEBROOT)/DEBIAN
	@# dpkg-shlibdeps wants this file, even if it is empty.
	$(Q)touch $(DEBDIR)/control
	$(Q)deps='$(DEB_DEPENDS)'; \
	[ -n "$$deps" ] || deps=`cd $(PKGDIR) && dpkg-shlibdeps -O debian/g0wm/usr/bin/g0wm | \
		sed -n 's/^shlibs:Depends=//p'`; \
	[ -n "$$deps" ] || { echo 'deb: dpkg-shlibdeps found no dependencies' >&2; exit 1; }; \
	sed -e 's|@VERSION@|$(PKGVER)|' -e 's|@ARCH@|$(DEB_ARCH)|' \
		-e 's|@MAINTAINER@|$(MAINTAINER)|' -e "s#@DEPENDS@#$$deps#" \
		-e "s|@SIZE@|`du -sk $(DEBROOT)/usr | cut -f1`|" \
		packaging/deb/control.in >$(DEBROOT)/DEBIAN/control
	$(Q)dpkg-deb --root-owner-group -b $(DEBROOT) $(PKGDIR)/g0wm_$(PKGVER)_$(DEB_ARCH).deb
	$(Q)rm -rf $(DEBDIR)

rpm: stage
	@printf '  RPM     %s\n' '$(PKGVER)'
	$(Q)mkdir -p $(PKGDIR)/rpm
	$(Q)sed -e 's|@VERSION@|$(PKGVER)|' -e 's|@ARCH@|$(MACHINE)|' \
		-e 's|@STAGE@|$(abspath $(PKGROOT))|' \
		packaging/rpm/g0wm.spec.in >$(PKGDIR)/rpm/g0wm.spec
	$(Q)rpmbuild -bb --define '_topdir $(abspath $(PKGDIR))/rpm' \
		$(PKGDIR)/rpm/g0wm.spec
	$(Q)cp $(PKGDIR)/rpm/RPMS/*/g0wm-*.rpm $(PKGDIR)/

# Skip formats whose tools are not installed.
packages: tarball
	$(Q)if command -v dpkg-deb >/dev/null; then $(MAKE) --no-print-directory deb; fi
	$(Q)if command -v rpmbuild >/dev/null; then $(MAKE) --no-print-directory rpm; fi
