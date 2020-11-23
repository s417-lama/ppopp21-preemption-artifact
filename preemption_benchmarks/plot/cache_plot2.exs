# mem_access = "seq_read"
# mem_access = "random_read"
mem_access = "seq_read_write"
# mem_access = "random_read_write"

# preemption_type = "yield"
preemption_type = "new_es"

nthreads = [56, 112, 224, 448, 896]

data_dir = "~/playground/results/cache_#{mem_access}_#{preemption_type}/latest"

linewidth = 2

get_data_in_file = fn path ->
    data =
        File.stream!(path)
        |> Stream.map(&String.split/1)
        |> Stream.map(fn
            ["pthread", _, _, time, _] ->
                {:pthread, String.to_integer(time) / 1_000_000_000}
            ["no", "preemption", _, _, time, _] ->
                {:no_preemption, String.to_integer(time) / 1_000_000_000}
            ["preemption", _, _, intvl, _, _, _, time, _] ->
                {{:preemption, String.to_integer(intvl)}, String.to_integer(time) / 1_000_000_000}
            _ -> nil
        end)
        |> Stream.filter(&!is_nil(&1))
        |> Enum.group_by(fn {key, _} -> key end, fn {_, value} -> value end)
        |> Enum.map(fn {key, [_ | times]} -> {key, Statistics.mean(times)} end)
        |> Map.new()
    %{no_preemption: baseline} = data
    Enum.map(data, fn {key, time} -> {key, max(0, time / baseline - 1)} end)
    |> Enum.filter(fn {key, _} -> key != :no_preemption end)
end

key2title = fn
    :pthread             -> "Pthread"
    # {:preemption, intvl} -> "Timer interval = #{:erlang.float_to_binary(intvl / 1000, [decimals: 1])} ms"
    {:preemption, intvl} -> "#{:erlang.float_to_binary(intvl / 1000, [decimals: 1])} ms"
end

key2color = fn
    :pthread             -> "#2ca02c"
    {:preemption, 1000 } -> "#e9a9ea"
    {:preemption, 1200 } -> "#c58ed7"
    {:preemption, 1400 } -> "#a274c1"
    {:preemption, 1800 } -> "#7f5baa"
    {:preemption, 2500 } -> "#5c4394"
    {:preemption, 4000 } -> "#382d7e"
    {:preemption, 10000} -> "#001969"
end

key2dash = fn
    :pthread         -> "dashdot"
    {:preemption, _} -> "solid"
end

key2symbol = fn
    :pthread         -> "square"
    {:preemption, _} -> "circle"
end

keysorter = fn
    {:pthread         , _}, {{:preemption, _} , _} -> false
    {{:preemption, _} , _}, {:pthread         , _} -> true
    {{:preemption, i1}, _}, {{:preemption, i2}, _} -> i1 < i2
end

nthreads
|> Enum.map(fn nthread ->
    "#{data_dir}/nthread_#{nthread}_*.out"
    |> Path.expand()
    |> Path.wildcard()
    |> Enum.map(get_data_in_file)
    |> List.flatten()
    |> Enum.group_by(fn {key, _} -> key end, fn {_, value} -> value end)
    |> Enum.map(fn {key, times} -> {key, times, nthread} end)
end)
|> List.flatten()
|> Enum.group_by(fn {key, _, _} -> key end, fn {_, times, nthread} -> {times, nthread} end)
|> Enum.sort(keysorter)
|> Enum.map(fn {key, data} ->
    {times, xs} = Enum.unzip(data)
    {ys, errors} =
        Enum.map(times, fn t -> {Statistics.mean(t), Statistics.stdev(t)} end)
        |> Enum.unzip()
    %{
        x: xs,
        y: ys,
        error_y: %{
            type:      "data",
            array:     errors,
            visible:   true,
            thickness: linewidth,
        },
        name: key2title.(key),
        line: %{
            width: linewidth,
            color: key2color.(key),
            dash: key2dash.(key),
        },
        marker: %{
            size: 10,
            # line: %{width: linewidth},
            symbol: key2symbol.(key)
        },
    }
end)
|> PlotlyEx.plot(%{
    width:  500,
    height: 400,
    xaxis: %{
        type: "log",
        showgrid: true,
        dtick: "D1",
        title: %{text: "Total Data Size (MB)"},
        tickangle: 0,
        showline: true,
        ticks: "outside",
        tickvals: [10,20,30,40,50,60,70,80,90,100,200,300,400,500,600,700,800,900,1000],
        ticktext: [10,"","","","","","","","",100,"","","","","","","","",1000],
        range: [1.7, 3.05],
    },
    yaxis: %{
        title: %{text: "Overhead"},
        tickformat: ",.0%",
        showline: true,
        # range: [0, 0.21],
        # range: [0, 1.5],
        range: [0, 0.51],
        # range: [0, 1.5],
        dtick: 0.1,
    },
    legend: %{
        x: 0,
        y: 1,
        bgcolor: "rgba(0,0,0,0)",
    },
    font: %{
        size: 22,
    },
    margin: %{
        l: 80,
        r: 10,
        b: 70,
        t: 10,
        pad: 0,
    },
})
|> PlotlyEx.show()
