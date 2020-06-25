/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <string.h>

#include "../util.h"

#if defined(__linux__)
	#include <limits.h>
	#include <stdint.h>
	#include <unistd.h>

        /* color format characters */
        const char *fmtred         =  "{f44336;}";
        const char *fmtsuperred    =  "{fff;f00;}";
        const char *fmtgreen       =  "{8BC34A;}";
        const char *fmtsupergreen  =  "{fff;1B5E20}";
        const char *fmtnormal      =  "{;3b4252}";

        const size_t NUM_ICON_DRAINING = 11;
        const char *icon_draining[]= {"\uf58d", "\uf579", "\uf57a", "\uf57b", "\uf57c", "\uf57d", "\uf57e","\uf580","\uf57f","\uf581", "\uf578"};
        const size_t NUM_ICON_CHARGING = 7;
        const char *icon_charging[]= {"\uf585", "\uf586", "\uf587", "\uf588", "\uf589", "\uf58a", "\uf584"};
        const char *icon_full = "\uf583";
        const char *icon_unkown = "\uf58b";

        const int  lim            = 20;
        const int  superlim       = 10;


	static const char *
	pick(const char *bat, const char *f1, const char *f2, char *path,
	     size_t length)
	{
		if (esnprintf(path, length, f1, bat) > 0 &&
		    access(path, R_OK) == 0) {
			return f1;
		}

		if (esnprintf(path, length, f2, bat) > 0 &&
		    access(path, R_OK) == 0) {
			return f2;
		}

		return NULL;
	}

        int
	battery_perc(const char *bat)
	{
		int perc;
		char path[PATH_MAX];

		if (esnprintf(path, sizeof(path),
		              "/sys/class/power_supply/%s/capacity", bat) < 0) {
			return 0;
		}
		if (pscanf(path, "%d", &perc) != 1) {
			return 0;
		}

		return perc;
	}

	const char *
	battery_state(const char *bat)
	{
		static struct {
			char *state;
			char *symbol;
		} map[] = {
			{ "Charging",    "+" },
			{ "Discharging", "-" },
			{ "Full",        "" },
		};
		size_t i;
		char path[PATH_MAX], state[12];

		if (esnprintf(path, sizeof(path),
		              "/sys/class/power_supply/%s/status", bat) < 0) {
			return NULL;
		}
		if (pscanf(path, "%12s", state) != 1) {
			return NULL;
		}

		for (i = 0; i < LEN(map); i++) {
			if (!strcmp(map[i].state, state)) {
				break;
			}
		}
		return (i == LEN(map)) ? "?" : map[i].symbol;
	}

	const char *
	battery_remaining(const char *bat)
	{
		uintmax_t charge_now, current_now, m, h;
		double timeleft;
		char path[PATH_MAX], state[12];

		if (esnprintf(path, sizeof(path),
		              "/sys/class/power_supply/%s/status", bat) < 0) {
			return NULL;
		}
		if (pscanf(path, "%12s", state) != 1) {
			return NULL;
		}

		if (!pick(bat, "/sys/class/power_supply/%s/charge_now",
		          "/sys/class/power_supply/%s/energy_now", path,
		          sizeof(path)) ||
		    pscanf(path, "%ju", &charge_now) < 0) {
			return NULL;
		}

		if (!strcmp(state, "Discharging")) {
			if (!pick(bat, "/sys/class/power_supply/%s/current_now",
			          "/sys/class/power_supply/%s/power_now", path,
			          sizeof(path)) ||
			    pscanf(path, "%ju", &current_now) < 0) {
				return NULL;
			}

			if (current_now == 0) {
				return NULL;
			}

			timeleft = (double)charge_now / (double)current_now;
			h = timeleft;
			m = (timeleft - (double)h) * 60;

			return bprintf("%juh %jum", h, m);
		}

		return "";
	}

        const char *
        battery_module(const char *bat)
        {
            const char *icon = icon_unkown;
            int perc = battery_perc(bat);
            float frac = perc/100.0f;
            char charging[2];
            char rem[50];

            strncpy(charging, battery_state(bat), 2);
            strncpy(rem, battery_remaining(bat), 50);

            const char *fmt = fmtnormal;
            if (*charging == '+')
            {
                fmt = fmtgreen;
                icon = icon_charging[interpolate(frac, 0, NUM_ICON_CHARGING)];
            }
            else if (perc == 100 && *charging != '-') /* */
            {
                fmt = fmtsupergreen;
                icon = icon_full;
            }
            else {
                icon = icon_draining[interpolate(frac, 0, NUM_ICON_DRAINING)];
                if (perc <= superlim)
                    fmt = fmtsuperred;
                else if (perc <= lim)
                    fmt = fmtred;
            }


            if (*rem) return bprintf("%s %s %s%d%% %s", fmt, icon, charging, perc, rem);
            else     return bprintf("%s %s %s%d%%",     fmt, icon, charging, perc);
        }

#elif defined(__OpenBSD__)
// {{{
	#include <fcntl.h>
	#include <machine/apmvar.h>
	#include <sys/ioctl.h>
	#include <unistd.h>

	static int
	load_apm_power_info(struct apm_power_info *apm_info)
	{
		int fd;

		fd = open("/dev/apm", O_RDONLY);
		if (fd < 0) {
			warn("open '/dev/apm':");
			return 0;
		}

		memset(apm_info, 0, sizeof(struct apm_power_info));
		if (ioctl(fd, APM_IOC_GETPOWER, apm_info) < 0) {
			warn("ioctl 'APM_IOC_GETPOWER':");
			close(fd);
			return 0;
		}
		return close(fd), 1;
	}

	const char *
	battery_perc(const char *unused)
	{
		struct apm_power_info apm_info;

		if (load_apm_power_info(&apm_info)) {
			return bprintf("%d", apm_info.battery_life);
		}

		return NULL;
	}

	const char *
	battery_state(const char *unused)
	{
		struct {
			unsigned int state;
			char *symbol;
		} map[] = {
			{ APM_AC_ON,      "+" },
			{ APM_AC_OFF,     "-" },
		};
		struct apm_power_info apm_info;
		size_t i;

		if (load_apm_power_info(&apm_info)) {
			for (i = 0; i < LEN(map); i++) {
				if (map[i].state == apm_info.ac_state) {
					break;
				}
			}
			return (i == LEN(map)) ? "?" : map[i].symbol;
		}

		return NULL;
	}

	const char *
	battery_remaining(const char *unused)
	{
		struct apm_power_info apm_info;

		if (load_apm_power_info(&apm_info)) {
			if (apm_info.ac_state != APM_AC_ON) {
				return bprintf("%uh %02um",
			                       apm_info.minutes_left / 60,
				               apm_info.minutes_left % 60);
			} else {
				return "";
			}
		}

		return NULL;
	}
#elif defined(__FreeBSD__)
	#include <sys/sysctl.h>

	const char *
	battery_perc(const char *unused)
	{
		int cap;
		size_t len;

		len = sizeof(cap);
		if (sysctlbyname("hw.acpi.battery.life", &cap, &len, NULL, 0) == -1
				|| !len)
			return NULL;

		return bprintf("%d", cap);
	}

	const char *
	battery_state(const char *unused)
	{
		int state;
		size_t len;

		len = sizeof(state);
		if (sysctlbyname("hw.acpi.battery.state", &state, &len, NULL, 0) == -1
				|| !len)
			return NULL;

		switch(state) {
			case 0:
			case 2:
				return "+";
			case 1:
				return "-";
			default:
				return "?";
		}
	}

	const char *
	battery_remaining(const char *unused)
	{
		int rem;
		size_t len;

		len = sizeof(rem);
		if (sysctlbyname("hw.acpi.battery.time", &rem, &len, NULL, 0) == -1
				|| !len
				|| rem == -1)
			return NULL;

		return bprintf("%uh %02um", rem / 60, rem % 60);
	}
// }}}
#endif
