import subprocess
from random import choice
import sys

dryrun = 0

def run_cmd(args, log):
    try:
        # join to produce a command
        cmd = " ".join(args)

        print "running: " + cmd
        if dryrun:
            return

        log.write("running: " + cmd + "\n")
        p = subprocess.Popen(cmd, shell=True, stdout= subprocess.PIPE, stderr= subprocess.PIPE)
        #p = Popen(cmd,shell=True,stdout=PIPE,stderr=STDOUT)
        (out,err) = p.communicate()
        print str(out)
        print str(err)
        log.write(str(out))
        log.write(str(err))
    
        if p.returncode == 0:
            print ("command '{}' succeeded.".format(cmd))
        elif p.returncode <= 125:
            print ("command '{}' failed, exit-code={}".format(cmd, p.returncode))
        elif p.returncode == 127:
            print ("program '{}' not found".format(args[0]))
        else:
            # Things get hairy and unportable - different shells return
            # different values for coredumps, signals, etc.
            sys.exit("'{}' likely crashed, shell retruned code {}".format(cmd, p.returncode))
    except OSError as e:
        # unlikely, but still possible: the system failed to execute the shell
        # itself (out-of-memory, out-of-file-descriptors, and other extreme cases).
        sys.exit("failed to run shell: '{}'".format(str(e)))



def run_cmd_get_line(args, log, options):
    # join to produce a command
    cmd = " ".join(args)
    print "running: " + cmd

    if dryrun:
        return

    try:
        log.write("running: " + cmd + "\n")
        p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        while True:
            line = p.stdout.readline()
            log.write(line)
            #print line,
            if line == '' and p.poll() != None:
                yield line
                if p.returncode != 0:
                    options.failure = 1
                    print("command failed.\n")
                # don't resent options.failure
                # only let it set at the beginning by caller
                #else:
                #    options.failure = 0
                break
            yield line
    except OSError as e:
        sys.exit("failed command: '{}'".format(str(e)))


def remote_ssh_cmd(host, userid, command, logfd):
    remote_cmd = "ssh " + userid + "@" + host + " \"sh -c '" + command + "'\""
    if command.dryrun:
        print(remote_cmd)
        return 0

    run_cmd([remote_cmd], logfd)


def createFolder(directory):
    try:
        if not os.path.exists(directory):
            os.makedirs(directory)
    except OSError:
        print ('Error: Creating directory. ' +  directory)


if __name__ == "__main__":
    import sys
    ## get arguments 1, 2, ....
    ## exclude 0, which is itself
    run_cmd(sys.argv[1:])
