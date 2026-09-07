# Byte-oriented (LC_ALL=C), shared by local capture and CI replay.
function routine(s) {
    return s ~ /^$/ || s ~ /^> Task :[^ ]+( (UP-TO-DATE|FROM-CACHE|SKIPPED|NO-SOURCE))?$/ || \
        s ~ /^\[[0-9]+\/[0-9]+\] (Compiling|Linking|Processing) / || \
        s ~ /^(Reusing configuration cache\.|Configuration cache entry (stored|reused)\.|BUILD SUCCESSFUL in |[0-9]+ actionable tasks:|Calculating task graph |Starting a Gradle Daemon|> watchapp@|> (node |eslint )|added [0-9]+ packages|[0-9]+ packages are looking for funding|  run `npm fund`|found 0 vulnerabilities|All checks passed!|OK$|Ran [0-9]+ tests in )/ || \
        s ~ /^[. -]+$/ || s ~ /^(GABBRO|EMERY) APP MEMORY USAGE$/ || \
        s ~ /^(Total size of resources:|Total footprint in RAM:|Free RAM available \(heap\):)/
}
function retain(s) {
    tail[++lines % 60] = s
    if (!routine(s)) {
        unfamiliar++
        if (!first_detail) first_detail=lines
        last_detail=lines
    }
    if (tolower(s) ~ /warning/) warnings++
    if (s ~ /^Ran [0-9]+ tests in /) tests = tests " python=" $2
    if (s ~ /^Verified [0-9]+ instrumentation tests /) tests=tests " instrumentation=" $2
    if (s ~ /^(Tests:|Reports:)/) reports=reports "\n  " s
    if (s ~ /^> Task /) {
        if (s ~ /UP-TO-DATE$/) uptodate++
        else if (s ~ /FROM-CACHE$/) cached++
        else if (s ~ /(SKIPPED|NO-SOURCE)$/) skipped++
        else executed++
    }
    if (s ~ /^(GABBRO|EMERY) APP MEMORY USAGE$/) platform=$1
    if (s ~ /^Total size of resources:/) resources=$5
    if (s ~ /^Total footprint in RAM:/) ram=$5
    if (s ~ /^Free RAM available \(heap\):/) memory=memory " " platform " resources=" resources " RAM=" ram " heap=" $5 "B;"
}
function excerpt(    i,n,s,header,notice,remaining,start) {
    # Persist the entire shared allowance, including labels and truncation notice.
    if ((getline used < (directory "/excerpt-budget")) > 0) {
        split(used, budget, " "); bytes=budget[1]; count=budget[2]
    }
    close(directory "/excerpt-budget")
    while ((getline seen < (directory "/excerpt-seen")) > 0) {
        if (seen == logfile) { close(directory "/excerpt-seen"); return }
    }
    close(directory "/excerpt-seen")
    header="--- " logfile " (full log; excerpt below) ---"
    notice="[truncated; read full log above]"
    remaining=4096-bytes-length(header)-length(notice)-2
    if (remaining <= 1 || count >= 57) return
    print header; bytes+=length(header)+1; count++
    n=lines
    start=n>56?n-55:1
    # Trim a confidently recognized routine prefix; keep the entire diagnostic
    # block with context. Otherwise use the bounded tail, without filtering lines.
    if (first_detail && n-first_detail<54 && n-last_detail<=4) {
        start=first_detail>2?first_detail-2:1
    }
    for (i=start; i<=n && count<59; i++) {
        s=tail[i%60]
        if (length(s)+1 > remaining) s=substr(s,1,remaining-1)
        print s; bytes+=length(s)+1; remaining-=length(s)+1; count++
        if (remaining<=1) break
    }
    if (start==1 && i>n) notice="[end excerpt; full log above]"
    print notice; bytes+=length(notice)+1; count++
    print bytes, count > directory "/excerpt-budget"
    if (close(directory "/excerpt-budget") != 0) exit 74
    print logfile >> directory "/excerpt-seen"
    if (close(directory "/excerpt-seen") != 0) exit 74
}
function finish(status,    summary) {
    if (!stage) return
    if (close(logfile) != 0) exit 74
    summary="[" (status==0?"pass":"FAIL " status) "] " stage " " systime()-started "s; warnings=" warnings+0 " unfamiliar=" unfamiliar+0 tests
    if (executed+uptodate+cached+skipped) summary=summary "; tasks executed=" executed+0 " up-to-date=" uptodate+0 " cached=" cached+0 " skipped=" skipped+0
    print summary "; log=" filename
    if (memory) print "  Memory (bytes):" memory
    if (reports) printf "%s\n", substr(reports,2)
    print filename "\t" stage "\t" status "\t" systime()-started "\t" warnings+0 "\t" unfamiliar+0 >> directory "/manifest.tsv"
    if (close(directory "/manifest.tsv") != 0) exit 74
    if (status != 0 && !failed_stage) failed_stage=stage
    if (status != 0 && verbose != "true") excerpt()
    fflush()
    stage=""
}
function supplemental(    candidate,entry) {
    while ((getline candidate < (directory "/failure-files.txt")) > 0) {
        if (failed_stage ~ /emery/ && candidate !~ /emery/) continue
        if (failed_stage ~ /gabbro/ && candidate !~ /gabbro/) continue
        logfile=candidate
        lines=first_detail=last_detail=0
        while ((getline entry < logfile) > 0) retain(entry)
        close(logfile)
        if (lines) excerpt()
    }
    close(directory "/failure-files.txt")
}
BEGIN {
    if (replay) {
        while ((getline record < (directory "/manifest.tsv")) > 0) {
            split(record, fields, "\t")
            if (fields[3] ~ /^[0-9]+$/ && fields[3] != "0") {
                failed_stage=fields[2]
                logfile=directory "/" fields[1]
                while ((getline entry < logfile) > 0) retain(entry)
                close(logfile)
                excerpt()
                break
            }
        }
        if (failed_stage) supplemental()
        exit
    }
}
/^\036plan\t/ { planned[++planned_count]=substr($0,7); next }
/^\036stage\t/ {
    finish(0)
    stage=substr($0,8); started=systime()
    visited[stage]=1
    filename=sprintf("%02d-%s.log", ++sequence, stage)
    logfile=directory "/" filename
    printf "" > logfile
    lines=first_detail=last_detail=warnings=unfamiliar=executed=uptodate=cached=skipped=0
    tests=memory=platform=reports=""
    print stage > directory "/active"
    if (close(directory "/active") != 0) exit 74
    print "[start] " stage; fflush(); next
}
/^\036result\t/ { finish(substr($0,9)+0); next }
{
    if (stage) {
        print $0 > logfile
        if (fflush(logfile) != 0) exit 74
        retain($0)
    }
    if (verbose == "true") { print; fflush() }
}
END {
    if (!replay) {
        finish(1)
        for (p=1; p<=planned_count; p++) {
            if (!(planned[p] in visited)) {
                print "[unstarted] " planned[p]
                print "-\t" planned[p] "\tunstarted\t-\t-\t-" >> directory "/manifest.tsv"
                unstarted=1
            }
        }
        if (unstarted && close(directory "/manifest.tsv") != 0) exit 74
        if (failed_stage && verbose != "true") supplemental()
    }
}
