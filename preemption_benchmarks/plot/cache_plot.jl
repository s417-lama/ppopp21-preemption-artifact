using Distributed
using PlotlyJS

host = ARGS[1]

remote_pid = addprocs([host], dir="/tmp", exename="julia", tunnel=true)[1]

@everywhere using Statistics

@everywhere function on_node()
    mem_access = "seq"
    #= mem_access = "random" =#

    no_preemption_dir = "~/playground/results/cache_$(mem_access)_no_preemption/latest"

    data_dirs = [
                 "~/playground/results/cache_$(mem_access)_preemption_new_es_intvl_1000/latest",
                 "~/playground/results/cache_$(mem_access)_preemption_new_es_intvl_1200/latest",
                 "~/playground/results/cache_$(mem_access)_preemption_new_es_intvl_1500/latest",
                 "~/playground/results/cache_$(mem_access)_preemption_new_es_intvl_2000/latest",
                 "~/playground/results/cache_$(mem_access)_preemption_new_es_intvl_5000/latest",
                 "~/playground/results/cache_$(mem_access)_preemption_new_es_intvl_10000/latest",
                 "~/playground/results/cache_$(mem_access)_pthread/latest",
                ]

    read_file = path -> begin
        m = match(r".*/nthread_(?<nthread>\d+)_iter_(?<iter>\d+)_(?<N>\d+).out$", path)
        nthread = parse(Int, m[:nthread])
        iter    = parse(Int, m[:iter])
        open(path) do file
            times = [parse(Int, split(line)[5])
                     for line in readlines(file) if occursin("elapsed", line)]
            (nthread, iter, times)
        end
    end

    read_dir = dir -> begin
        files = dir |> expanduser |> readdir
        files = filter(file -> occursin(r".*\.out$", file), files)
        data = map(files) do file
            (nthread, iter, times) = "$dir/$file" |> expanduser |> read_file
            #= (nthread, median(times[3:end])) =#
            (nthread, minimum(times[3:end]))
        end
        # group by
        dict = Dict()
        for (nthread, time) in data
            dict[nthread] = append!(get(dict, nthread, []), time)
        end
        #= [(nthread, median(times)) for (nthread, times) in dict] |> sort =#
        [(nthread, minimum(times)) for (nthread, times) in dict] |> sort
    end

    no_preemption_data = read_dir(no_preemption_dir)

    map(data_dirs) do dir
        data = read_dir(dir)
        data = map(zip(data, no_preemption_data)) do ((nthread, time), (_, base_time))
            #= (nthread, max(time / base_time - 1, 0)) =#
            (nthread, time / base_time - 1)
        end
        xs = getindex.(data, 1)
        ys = getindex.(data, 2)
        (xs, ys)
    end
end

ret = fetch(@spawnat remote_pid on_node())

opts = [
        ("intvl = 1.0 ms" , "solid"  ),
        ("intvl = 1.2 ms" , "solid"  ),
        ("intvl = 1.5 ms" , "solid"  ),
        ("intvl = 2.0 ms" , "solid"  ),
        ("intvl = 5.0 ms" , "solid"  ),
        ("intvl = 10.0 ms", "solid"  ),
        ("pthread"        , "dashdot"),
       ]

traces = map(zip(ret, opts)) do ((xs, ys), (title, dash))
    scatter(
        x = xs,
        y = ys,
        name = title,
        line = attr(dash = dash),
        marker = attr(size = 10),
    )
end

layout = Layout(
    width = 650,
    height = 500,
    xaxis = attr(
        type = :log,
        showgrid = true,
        dtick = :D1,
        title = attr(text = "Total Data Size (MB)"),
        tickangle = 0,
        showline = true,
        ticks = :outside,
        tickvals = [10,20,30,40,50,60,70,80,90,100,200,300,400,500,600,700,800,900,1000],
        ticktext = [10,"","","","","","","","",100,"","","","","","","","",1000],
        range = [1.7, 3.05],
    ),
    yaxis = attr(
        title = attr(text = "Overhead"),
        tickformat = ",.0%",
        showline = true,
    ),
    legend = attr(x = 0, y = 1),
    font = attr(size = 18),
    margin = attr(l = 75, r = 0, b = 60, t = 10, pad = 0),
)

options = Dict(:toImageButtonOptions => Dict(:format => :svg))

plot(traces, layout, options = options) |> display
